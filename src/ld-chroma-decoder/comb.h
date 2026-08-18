/************************************************************************

    comb.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018 Chad Page
    Copyright (C) 2018-2019 Simon Inns
    Copyright (C) 2020-2021 Adam Sampson
    Copyright (C) 2021 Phillip Blucas

    This file is part of tbc-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#ifndef COMB_H
#define COMB_H
#ifndef LDCHROMA_HAS_CUDA
#define LDCHROMA_HAS_CUDA 0
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QtMath>
#include <atomic>

#include "lddecodemetadata.h"

#include "componentframe.h"
#include "decoder.h"
#include "sourcefield.h"


#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>
#if LDCHROMA_HAS_CUDA
#include <cufft.h>

// Forward declaration; implementation is in the .cu file
void launch_nnTransform3D_CUDA(
    uint16_t* d_cvbs_f0, uint16_t* d_cvbs_f1, bool pad_f0, bool pad_f1,
    double* d_accChroma_f0, double* d_accChroma_f1, double* d_weightSum_f0, double* d_weightSum_f1,
    cufftHandle p_fwd, cufftHandle p_inv, cufftDoubleComplex* d_in_batch, cufftDoubleComplex* d_out_batch,
    float* d_trt_input, float* d_mask,
    int* d_ledger_y, int* d_ledger_x, double* d_ledger_dc,
    double* d_winX, double* d_winY, double* d_winT,
    int num_blocks, size_t block_size, int activeStartX, int activeEndX,
    Ort::Session* session);

class nnTransform3DCUDA {
public:
    nnTransform3DCUDA(int activeStart, int activeEnd, const char* modelPath);
    ~nnTransform3DCUDA();

    
    void processFrame(const uint16_t* inputFrame, double* outChromaDouble);

private:
    int activeStartX, activeEndX;

    // ONNX Runtime resources
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;

    // CUDA & cuFFT resources
    cufftHandle p_fwd, p_inv;
    bool is_plan_created = false;
    int cached_num_blocks = 0;

    // Device-memory pointers (replace the old static variables)
    cufftDoubleComplex* d_in_batch = nullptr, * d_out_batch = nullptr;
    float* d_trt_input = nullptr, * d_mask = nullptr;
    int* d_ledger_y = nullptr, * d_ledger_x = nullptr;
    double* d_ledger_dc = nullptr, * d_winX = nullptr, * d_winY = nullptr, * d_winT = nullptr;

    // Internal frame buffers (manage LookBehind and LookAhead)
    struct InternalFrame {
        bool isPadding = true;
        std::vector<uint16_t> cvbs;
        uint16_t* d_cvbs = nullptr;
        double* d_accChroma = nullptr;
        double* d_weightSum = nullptr;
        // Host-side accumulation buffers
        std::vector<double> h_accChroma;
        std::vector<double> h_weightSum;
    };

    InternalFrame frame0, frame1;
    bool isFirstFrame = true;

    void initGPUResources(int num_blocks);
    void freeGPUResources();
    void allocateFrameBuffer(InternalFrame& f);
    void resetFrameOLA(InternalFrame& f);
};
#endif

class Comb
{
public:
    Comb();

    // Comb filter configuration parameters
    struct Configuration {
        double chromaGain = 1.0;
        double chromaPhase = 0.0;
        qint32 dimensions = 2;
        bool nnTransform3D = false;
        bool adaptive = true;
        bool showMap = false;
        bool phaseCompensation = false;
        bool useNNTransform3D = false;

        double cNRLevel = 0.0;
        double yNRLevel = 0.0;

        // Adaptation sensitivity for 3D filter (higher = prefer 3D, lower = prefer 1D/2D)
        // Default 1.0, range typically 0.5-2.0
        double adaptThreshold = 1.0;
        double chromaWeight = 1.0;

        qint32 getLookBehind() const;
        qint32 getLookAhead() const;
    };

    const Configuration &getConfiguration() const;
    void updateConfiguration(const LdDecodeMetaData::VideoParameters &videoParameters,
                             const Configuration &configuration);

    // Decode a sequence of fields into a sequence of interlaced frames
    void decodeFrames(const QVector<SourceField> &inputFields, qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames);
    void requestNnTransform3DCancel();

    // Maximum frame size
    static constexpr qint32 MAX_WIDTH = 910;
    static constexpr qint32 MAX_HEIGHT = 525;

protected:

private:
    // Comb-filter configuration parameters
    bool configurationSet;
    Configuration configuration;
    LdDecodeMetaData::VideoParameters videoParameters;
    std::atomic<quint64> nnTransform3DCancelEpoch {0};

    // An input frame in the process of being decoded
    class FrameBuffer {
    public:
        FrameBuffer(const LdDecodeMetaData::VideoParameters &videoParameters_, const Configuration &configuration_);

        void loadFields(const SourceField &firstField, const SourceField &secondField);
        void copyRawToLuma();

        const quint16* getRawBuffer() const { return rawbuffer.data(); }
        
        void applyGPUChroma(const double* gpuOutChromaDouble, qint32 activeStart, qint32 activeEnd, qint32 firstLine, qint32 lastLine);

        void split1D();
        void split2D();
        void split3D(const FrameBuffer &previousFrame, const FrameBuffer &nextFrame);
        bool split3DnnTransform(FrameBuffer &nextFrame, qint32 frameIndex,
                                const std::atomic<quint64> &cancelEpoch, quint64 decodeEpoch);
        void finalizeNnTransform3D();
        void fallbackNnTransform3DTo2D();

        void setComponentFrame(ComponentFrame &_componentFrame) {
            componentFrame = &_componentFrame;
        }

        void splitIQ();
        void splitIQlocked();
        void filterIQ();
        void filterIQFull();
        void adjustY();
        void doCNR();
        void doYNR();
        void transformIQ(double chromaGain, double chromaPhase);

        void overlayMap(const FrameBuffer &previousFrame, const FrameBuffer &nextFrame);

    private:
        const LdDecodeMetaData::VideoParameters &videoParameters;
        const Configuration &configuration;

        // Calculated frame height
        qint32 frameHeight;

        // IRE scaling
        double irescale;

        // Baseband samples (interlaced to form a complete frame)
        SourceVideo::Data rawbuffer;

        QVector<QVector<double>> nnAccChroma;
        QVector<QVector<double>> nnWeightSum;

        // Chroma phase of the frame's two fields
        qint32 firstFieldPhaseID;
        qint32 secondFieldPhaseID;

        // 1D, 2D and 3D-filtered chroma samples
        struct Sample {
            double pixel[MAX_HEIGHT][MAX_WIDTH];
        } clpbuffer[3];

        // Result of evaluating a 3D candidate
        struct Candidate {
            double penalty;
            double sample;
        };

        // The component frame for output (if there is one)
        ComponentFrame *componentFrame;

        inline qint32 getFieldID(qint32 lineNumber) const;
        inline bool getLinePhase(qint32 lineNumber) const;
        void getBestCandidate(qint32 lineNumber, qint32 h,
                              const FrameBuffer &previousFrame, const FrameBuffer &nextFrame,
                              qint32 &bestIndex, double &bestSample) const;
        Candidate getCandidate(qint32 refLineNumber, qint32 refH,
                               const FrameBuffer &frameBuffer, qint32 lineNumber, qint32 h,
                               double adjustPenalty) const;
    };
};

#endif // COMB_H
