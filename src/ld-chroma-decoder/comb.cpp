/************************************************************************

    comb.cpp

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

#include "comb.h"

#include "framecanvas.h"

#include "deemp.h"
#include "firfilter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef __linux__
#include <dlfcn.h>
#include <link.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include <fftw3.h>
#include <onnxruntime_cxx_api.h>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include "chroma_net_v2_onnx_data.h"
#include "tbc/logging.h"


#include <iostream>
#if LDCHROMA_HAS_CUDA
#include <cuda_runtime.h>
#endif
// Constants
#if LDCHROMA_HAS_CUDA
const int FRAME_WIDTH = 910;
const int FRAME_HEIGHT = 526;
const int Nx = 16, Ny = 16, Nt = 4;
#endif

// Indexes for the candidates considered in 3D adaptive mode
enum CandidateIndex : qint32 {
    CAND_LEFT,
    CAND_RIGHT,
    CAND_UP,
    CAND_DOWN,
    CAND_PREV_FIELD,
    CAND_NEXT_FIELD,
    CAND_PREV_FRAME,
    CAND_NEXT_FRAME,
    NUM_CANDIDATES
};

// Map colours for the candidates
static constexpr quint32 CANDIDATE_SHADES[] = {
    0xFF8080, // CAND_LEFT - red
    0xFF8080, // CAND_RIGHT - red
    0xFFFF80, // CAND_UP - yellow
    0xFFFF80, // CAND_DOWN - yellow
    0x80FF80, // CAND_PREV_FIELD - green
    0x80FF80, // CAND_NEXT_FIELD - green
    0x8080FF, // CAND_PREV_FRAME - blue
    0xFF80FF, // CAND_NEXT_FRAME - purple
};

// Since we are at exactly 4fsc, calculating the value of a in-phase sine wave at a specific position
// is very simple.
static constexpr double sin4fsc_data[] = {1.0, 0.0, -1.0, 0.0};

// 4fsc sine wave
constexpr double sin4fsc(const qint32 i) {
    return sin4fsc_data[i % 4];
}

// 4fsc cos wave
constexpr double cos4fsc(const qint32 i) {
    // cos(rad) is just sin(rad + pi/2) and we are at 4 fsc.
    return sin4fsc(i + 1);
}

#ifndef IDX3
#define IDX3(t, y, x, Nt, Ny, Nx) ((t) * (Ny) * (Nx) + (y) * (Nx) + (x))
#endif

namespace {
constexpr const char *kNnProviderEnvVar = "LDDECODE_NNTRANSFORM3D_PROVIDER";
constexpr const char *kNnThreadsEnvVar = "LDDECODE_NNTRANSFORM3D_THREADS";
constexpr const char *kOnnxRuntimeRootEnvVar = "ONNXRUNTIME_ROOT";

enum class NnExecutionProviderPreference {
    Auto,
    Cpu,
    Cuda,
    CoreML
};

NnExecutionProviderPreference getNnExecutionProviderPreference()
{
    const QString provider = qEnvironmentVariable(kNnProviderEnvVar).trimmed().toLower();
    if (provider.isEmpty() || provider == "auto") {
        return NnExecutionProviderPreference::Auto;
    }
    if (provider == "cpu") {
        return NnExecutionProviderPreference::Cpu;
    }
    if (provider == "cuda" || provider == "gpu") {
        return NnExecutionProviderPreference::Cuda;
    }
    if (provider == "coreml") {
        return NnExecutionProviderPreference::CoreML;
    }

    qWarning() << "Unknown" << kNnProviderEnvVar << "value:" << provider << "- using auto";
    return NnExecutionProviderPreference::Auto;
}

QString providerPreferenceToString(NnExecutionProviderPreference preference)
{
    switch (preference) {
    case NnExecutionProviderPreference::Auto:
        return QStringLiteral("auto");
    case NnExecutionProviderPreference::Cpu:
        return QStringLiteral("cpu");
    case NnExecutionProviderPreference::Cuda:
        return QStringLiteral("cuda");
    case NnExecutionProviderPreference::CoreML:
        return QStringLiteral("coreml");
    }
    return QStringLiteral("auto");
}

qint32 getNnIntraOpThreads()
{
    bool isSet = false;
    const qint32 value = qEnvironmentVariableIntValue(kNnThreadsEnvVar, &isSet);
    if (!isSet) {
        return 0; // Let ONNX Runtime choose a default.
    }
    if (value < 0) {
        qWarning() << "Ignoring negative" << kNnThreadsEnvVar << "value:" << value;
        return 0;
    }
    return value;
}
// Candidate directories where the opt-in CUDA plugin (a runtime package of the
// CUDA 11.8 + cuDNN 8.9 DLLs/SOs + the ORT CUDA EP provider library) may be
// installed. Searched front-to-back; the first dir that contains the provider
// library wins. An explicit TBC_CUDA_PLUGIN_DIR env var wins over all defaults
// so power users / CI can point at a staged plugin without installing it.
//
// The primary default is a writable, update-persistent location:
//   Linux:   ~/.local/share/tbc-tools/plugins/cuda  (XDG_DATA_HOME)
//   Windows: %LOCALAPPDATA%/tbc-tools/plugins/cuda
//   macOS:   ~/Library/Application Support/tbc-tools/plugins/cuda
// Use GenericDataLocation + a fixed "/tbc-tools/plugins/cuda" suffix (NOT
// AppDataLocation, which appends the per-binary application name and would
// put ld-analyse's plugin dir at a different path than ld-chroma-decoder's).
// The app-relative plugins/cuda + cuda-plugin paths are kept as fallbacks for
// portable/unzipped builds where the app dir is writable.
QStringList cudaPluginCandidateDirectories()
{
    QStringList dirs;
    const QString envDir = qEnvironmentVariable("TBC_CUDA_PLUGIN_DIR").trimmed();
    if (!envDir.isEmpty()) {
        dirs.append(QDir::cleanPath(envDir));
    }
    const QString genericData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!genericData.isEmpty()) {
        dirs.append(QDir(genericData).filePath(QStringLiteral("tbc-tools/plugins/cuda")));
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        dirs.append(QDir(appDir).filePath(QStringLiteral("plugins/cuda")));
        dirs.append(QDir(appDir).filePath(QStringLiteral("cuda-plugin")));
    }
    dirs.removeAll(QString());
    dirs.removeDuplicates();
    return dirs;
}

bool ensureCudaDriverLoaded(QString &errorMessage)
{
#ifdef __linux__
    static std::once_flag cudaDriverLoadOnce;
    static bool cudaDriverReady = false;
    static QString cudaDriverError;

    std::call_once(cudaDriverLoadOnce, []() {
        auto tryLoad = [](const char *path, QString &capturedError, bool rejectCudaStubs = false) -> bool {
            dlerror(); // Clear any stale state first.
            void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
            if (handle != nullptr) {
                if (rejectCudaStubs) {
                    void *cuInitSymbol = dlsym(handle, "cuInit");
                    if (cuInitSymbol != nullptr) {
                        Dl_info symbolInfo {};
                        if (dladdr(cuInitSymbol, &symbolInfo) != 0 && symbolInfo.dli_fname != nullptr) {
                            const QString loadedPath = QString::fromUtf8(symbolInfo.dli_fname);
                            if (loadedPath.contains("/stubs/")) {
                                capturedError = QStringLiteral("resolved to CUDA stub library: %1").arg(loadedPath);
                                dlclose(handle);
                                return false;
                            }
                        }
                    }
                }
                return true;
            }
            const char *message = dlerror();
            if (message != nullptr) {
                capturedError = QString::fromUtf8(message);
            }
            return false;
        };

        QString libcudaError;
        if (!(tryLoad("/lib/x86_64-linux-gnu/libcuda.so.1", libcudaError, true) ||
              tryLoad("/usr/lib/x86_64-linux-gnu/libcuda.so.1", libcudaError, true) ||
              tryLoad("libcuda.so.1", libcudaError, true))) {
            if (libcudaError.isEmpty()) {
                libcudaError = QStringLiteral("unable to locate libcuda.so.1");
            }
            cudaDriverError = QStringLiteral("CUDA driver load failed: %1").arg(libcudaError);
            return;
        }

        QString ptxJitError;
        if (!(tryLoad("/lib/x86_64-linux-gnu/libnvidia-ptxjitcompiler.so.1", ptxJitError) ||
              tryLoad("/usr/lib/x86_64-linux-gnu/libnvidia-ptxjitcompiler.so.1", ptxJitError) ||
              tryLoad("libnvidia-ptxjitcompiler.so.1", ptxJitError))) {
            if (ptxJitError.isEmpty()) {
                ptxJitError = QStringLiteral("unable to locate libnvidia-ptxjitcompiler.so.1");
            }
            cudaDriverError = QStringLiteral("CUDA PTX JIT compiler load failed: %1").arg(ptxJitError);
            return;
        }

        cudaDriverReady = true;
    });

    if (!cudaDriverReady) {
        errorMessage = QStringLiteral("CUDA driver library load failed: %1").arg(cudaDriverError);
        return false;
    }
#else
    Q_UNUSED(errorMessage)
#endif
    return true;
}

// On Linux, ORT loads libonnxruntime_providers_cuda.so itself via dlopen. That
// SO transitively needs libcudart/libcufft/libcublas/libcudnn, which on a clean
// machine are not on the loader search path. glibc caches LD_LIBRARY_PATH at
// startup, so setenv() after startup does not help. The reliable approach: dlopen
// each .so in the plugin dir by full path with RTLD_NOW | RTLD_GLOBAL before ORT
// resolves the CUDA EP, so the provider SO's DT_NEEDED entries resolve from the
// already-globally-loaded set. ORT's own dlopen("libonnxruntime_providers_cuda.so")
// then finds it already loaded (by soname) and does not re-search.
bool ensureLinuxOnnxCudaProviderLoaded(QString &errorMessage)
{
#ifdef __linux__
    static std::once_flag pluginLoadOnce;
    static bool pluginReady = false;
    static QString pluginError;
    static bool attempted = false;

    if (attempted) {
        if (!pluginReady) {
            errorMessage = pluginError;
        }
        return pluginReady;
    }

    std::call_once(pluginLoadOnce, []() {
        attempted = true;
        const QStringList pluginDirs = cudaPluginCandidateDirectories();
        QString resolvedPluginDir;
        QString providerSoPath;
        constexpr const char *kProviderSo = "libonnxruntime_providers_cuda.so";
        for (const QString &dir : pluginDirs) {
            const QDir pluginDir(dir);
            const QString candidate = pluginDir.filePath(QString::fromUtf8(kProviderSo));
            if (QFileInfo::exists(candidate)) {
                resolvedPluginDir = dir;
                providerSoPath = candidate;
                break;
            }
        }
        if (providerSoPath.isEmpty()) {
            // No plugin installed. Not an error -- ORT will fall back to CPU or
            // find the provider via its own search path (ONNXRUNTIME_ROOT/lib).
            pluginReady = true;
            return;
        }

        // Dlopen the CUDA runtime .so's in the plugin dir with RTLD_GLOBAL so
        // the provider SO's transitive deps resolve. Scan in dependency order:
        // cudart first, then cufft/cublas/cudnn, then the provider last.
        const QStringList runtimeSonamePrefixes = {
            QStringLiteral("libcudart.so"),
            QStringLiteral("libcufft.so"),
            QStringLiteral("libcurand.so"),
            QStringLiteral("libcublasLt.so"),
            QStringLiteral("libcublas.so"),
            QStringLiteral("libcudnn_ops_infer.so"),
            QStringLiteral("libcudnn_cnn_infer.so"),
            QStringLiteral("libcudnn.so"),
        };
        const QDir pluginDir(resolvedPluginDir);
        const QFileInfoList entries = pluginDir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const QString &prefix : runtimeSonamePrefixes) {
            for (const QFileInfo &entry : entries) {
                const QString name = entry.fileName();
                if (name.startsWith(prefix)) {
                    dlerror();
                    void *handle = dlopen(entry.absoluteFilePath().toUtf8().constData(), RTLD_NOW | RTLD_GLOBAL);
                    if (handle == nullptr) {
                        const char *msg = dlerror();
                        pluginError = QStringLiteral("failed to load CUDA plugin %1: %2")
                            .arg(name, QString::fromUtf8(msg != nullptr ? msg : "unknown dlopen error"));
                        return;
                    }
                    break; // one match per prefix
                }
            }
        }
        // Don't pre-load the provider .so ourselves. The provider .so has
        // BIND_NOW and references Provider_GetHost, a hidden symbol in the
        // main libonnxruntime.so that can only be resolved through ORT's
        // internal provider bridge (not standard dynamic linking). Pre-loading
        // it via dlopen fails (undefined symbol) or segfaults (if BIND_NOW is
        // patched out and the deferred symbol is called).
        //
        // Instead: set LD_LIBRARY_PATH to include the plugin dir so ORT's own
        // dlopen("libonnxruntime_providers_cuda.so") can find it. glibc caches
        // LD_LIBRARY_PATH at startup, but setenv() before the FIRST dlopen call
        // in the process can still influence the search path on some glibc
        // versions. If that fails, ORT will fall back to CPU (graceful).
        const QString currentLdLibPath = qgetenv("LD_LIBRARY_PATH").constData();
        QString newLdLibPath = resolvedPluginDir;
        if (!currentLdLibPath.isEmpty()) {
            newLdLibPath += QStringLiteral(":") + currentLdLibPath;
        }
        qputenv("LD_LIBRARY_PATH", newLdLibPath.toUtf8());
        tbcDebugStream() << "ensureLinuxOnnxCudaProviderLoaded(): set LD_LIBRARY_PATH to include"
                         << resolvedPluginDir
                         << "(CUDA runtime .so pre-loaded with RTLD_GLOBAL)";
        pluginReady = true;
    });

    if (!pluginReady) {
        errorMessage = pluginError;
    }
    return pluginReady;
#else
    Q_UNUSED(errorMessage)
    return true;
#endif
}

bool ensureWindowsOnnxCudaProviderLoaded(QString &errorMessage)
{
#ifdef _WIN32
    static std::once_flag providerLoadOnce;
    static bool providerReady = false;
    static QString providerError;

    std::call_once(providerLoadOnce, []() {
        auto formatWindowsError = [](DWORD errorCode) {
            LPWSTR messageBuffer = nullptr;
            const DWORD formatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS;
            const DWORD messageLength = FormatMessageW(
                formatFlags,
                nullptr,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPWSTR>(&messageBuffer),
                0,
                nullptr
            );

            QString message;
            if (messageLength > 0 && messageBuffer != nullptr) {
                message = QString::fromWCharArray(messageBuffer, static_cast<int>(messageLength)).trimmed();
            } else {
                message = QStringLiteral("Windows error code %1").arg(errorCode);
            }

            if (messageBuffer != nullptr) {
                LocalFree(messageBuffer);
            }
            return message;
        };

        auto tryLoadLibrary = [&](const QString &libraryPath, QString &loadError) -> bool {
            HMODULE module = LoadLibraryW(reinterpret_cast<LPCWSTR>(libraryPath.utf16()));
            if (module != nullptr) {
                return true;
            }

            loadError = formatWindowsError(GetLastError());
            return false;
        };

        auto tryLoadProviderPair = [&](const QString &directory, QString &pairError) -> bool {
            const QString providersSharedDll = QStringLiteral("onnxruntime_providers_shared.dll");
            const QString providersCudaDll = QStringLiteral("onnxruntime_providers_cuda.dll");
            const QString sharedPath = directory.isEmpty()
                ? providersSharedDll
                : QDir(directory).filePath(providersSharedDll);
            const QString cudaPath = directory.isEmpty()
                ? providersCudaDll
                : QDir(directory).filePath(providersCudaDll);

            if (!directory.isEmpty()) {
                if (!QFileInfo::exists(sharedPath) || !QFileInfo::exists(cudaPath)) {
                    pairError = QStringLiteral("missing ONNX Runtime CUDA provider DLLs in %1").arg(directory);
                    return false;
                }
            }

            // For a plugin directory, add it to the DLL search path so the
            // provider DLL's transitive CUDA runtime imports (cudart64_110,
            // cublas64_11, cufft64_10, cudnn64_8, ...) resolve from the plugin
            // dir rather than only the exe dir / system dirs. SetDllDirectoryW
            // is process-global and applies to subsequent LoadLibrary calls +
            // their transitive DT_NEEDED resolution. Reset to nullptr after so
            // we don't permanently alter the process search path.
            if (!directory.isEmpty()) {
                SetDllDirectoryW(reinterpret_cast<LPCWSTR>(directory.utf16()));
            }

            QString sharedError;
            if (!tryLoadLibrary(sharedPath, sharedError)) {
                pairError = QStringLiteral("failed to load %1: %2").arg(sharedPath, sharedError);
                if (!directory.isEmpty()) SetDllDirectoryW(nullptr);
                return false;
            }

            QString cudaError;
            if (!tryLoadLibrary(cudaPath, cudaError)) {
                pairError = QStringLiteral("failed to load %1: %2").arg(cudaPath, cudaError);
                if (!directory.isEmpty()) SetDllDirectoryW(nullptr);
                return false;
            }

            if (!directory.isEmpty()) SetDllDirectoryW(nullptr);
            return true;
        };

        QStringList candidateDirectories;
        // Plugin dirs first: if a CUDA plugin is installed there, its provider
        // DLL + transitive CUDA runtime DLLs resolve from the plugin dir via
        // SetDllDirectoryW (applied inside tryLoadProviderPair for a non-empty
        // plugin dir). This is the opt-in GPU path.
        const QStringList pluginDirs = cudaPluginCandidateDirectories();
        for (const QString &dir : pluginDirs) {
            candidateDirectories.append(dir);
        }

        const QString appDir = QCoreApplication::applicationDirPath();
        if (!appDir.isEmpty()) {
            candidateDirectories.append(appDir);
        }

        const QString onnxRuntimeRoot = qEnvironmentVariable(kOnnxRuntimeRootEnvVar).trimmed();
        if (!onnxRuntimeRoot.isEmpty()) {
            const QDir onnxRuntimeRootDir(onnxRuntimeRoot);
            candidateDirectories.append(onnxRuntimeRootDir.filePath(QStringLiteral("lib")));
            candidateDirectories.append(onnxRuntimeRootDir.filePath(QStringLiteral("bin")));
            candidateDirectories.append(onnxRuntimeRoot);
        }

        candidateDirectories.removeDuplicates();
        candidateDirectories.append(QString());

        QString pairError;
        for (const QString &candidateDirectory : candidateDirectories) {
            if (tryLoadProviderPair(candidateDirectory, pairError)) {
                providerReady = true;
                return;
            }
        }

        providerError = pairError.isEmpty()
            ? QStringLiteral("unable to locate ONNX Runtime CUDA provider DLLs")
            : pairError;
    });

    if (!providerReady) {
        errorMessage = providerError;
        return false;
    }
#else
    Q_UNUSED(errorMessage)
#endif
    return true;
}

bool appendCudaExecutionProvider(Ort::SessionOptions &options, QString &errorMessage)
{
    if (!ensureCudaDriverLoaded(errorMessage)) {
        return false;
    }
    if (!ensureWindowsOnnxCudaProviderLoaded(errorMessage)) {
        return false;
    }
    if (!ensureLinuxOnnxCudaProviderLoaded(errorMessage)) {
        return false;
    }

    const auto &ortApi = Ort::GetApi();
    OrtCUDAProviderOptionsV2 *cudaOptions = nullptr;
    OrtStatus *status = ortApi.CreateCUDAProviderOptions(&cudaOptions);
    if (status != nullptr) {
        errorMessage = QStringLiteral("CreateCUDAProviderOptions failed: %1")
                           .arg(QString::fromUtf8(ortApi.GetErrorMessage(status)));
        ortApi.ReleaseStatus(status);
        return false;
    }

    auto releaseCudaOptions = [&]() {
        if (cudaOptions != nullptr) {
            ortApi.ReleaseCUDAProviderOptions(cudaOptions);
            cudaOptions = nullptr;
        }
    };

    auto updateCudaProviderOptions = [&](const std::vector<std::pair<const char *, const char *>> &entries,
                                         QString &updateError) -> bool {
        std::vector<const char *> keys;
        std::vector<const char *> values;
        keys.reserve(entries.size());
        values.reserve(entries.size());
        for (const auto &entry : entries) {
            keys.push_back(entry.first);
            values.push_back(entry.second);
        }

        OrtStatus *updateStatus = ortApi.UpdateCUDAProviderOptions(
            cudaOptions,
            keys.data(),
            values.data(),
            keys.size()
        );
        if (updateStatus != nullptr) {
            updateError = QString::fromUtf8(ortApi.GetErrorMessage(updateStatus));
            ortApi.ReleaseStatus(updateStatus);
            return false;
        }
        return true;
    };

    auto removeUnsupportedCudaOption = [](
        std::vector<std::pair<const char *, const char *>> &entries,
        const QString &updateError,
        QString &optionName
    ) -> bool {
        static const QRegularExpression unknownOptionRegex(
            QStringLiteral("Unknown provider option:\\s*\"([^\"]+)\"")
        );
        const QRegularExpressionMatch match = unknownOptionRegex.match(updateError);
        if (!match.hasMatch()) {
            return false;
        }

        optionName = match.captured(1);
        const qsizetype previousSize = entries.size();
        entries.erase(
            std::remove_if(entries.begin(), entries.end(), [&](const auto &entry) {
                return optionName == QLatin1String(entry.first);
            }),
            entries.end()
        );
        return entries.size() != static_cast<size_t>(previousSize);
    };

    auto applyCudaProviderOptions = [&](std::vector<std::pair<const char *, const char *>> entries,
                                        QString &applyError) -> bool {
        while (true) {
            if (updateCudaProviderOptions(entries, applyError)) {
                return true;
            }

            QString unsupportedOption;
            if (!removeUnsupportedCudaOption(entries, applyError, unsupportedOption)) {
                return false;
            }

            qWarning() << "Ignoring unsupported CUDA provider option for nnTransform3D:"
                       << unsupportedOption;

            if (entries.empty()) {
                applyError = QStringLiteral("no supported CUDA provider options remained after filtering");
                return false;
            }
        }
    };

    const std::vector<std::pair<const char *, const char *>> preferredEntries = {
        { "device_id", "0" },
        { "cudnn_conv_algo_search", "EXHAUSTIVE" },
        { "cudnn_conv_use_max_workspace", "0" },
        { "do_copy_in_default_stream", "1" },
        { "tunable_op_enable", "0" },
        { "tunable_op_tuning_enable", "0" },
        { "use_tf32", "0" },
        { "prefer_nhwc", "0" },
    };

    const std::vector<std::pair<const char *, const char *>> compatibilityEntries = {
        { "device_id", "0" },
        { "cudnn_conv_algo_search", "EXHAUSTIVE" },
        { "cudnn_conv_use_max_workspace", "0" },
        { "do_copy_in_default_stream", "1" },
        { "tunable_op_enable", "0" },
        { "tunable_op_tuning_enable", "0" },
    };

    QString updateError;
    if (!applyCudaProviderOptions(preferredEntries, updateError)) {
        qWarning() << "Unable to apply preferred CUDA provider options for nnTransform3D; retrying with compatibility set:"
                   << updateError;
        if (!applyCudaProviderOptions(compatibilityEntries, updateError)) {
            errorMessage = QStringLiteral("UpdateCUDAProviderOptions failed: %1").arg(updateError);
            releaseCudaOptions();
            return false;
        }
    }

    status = ortApi.SessionOptionsAppendExecutionProvider_CUDA_V2(options, cudaOptions);
    if (status != nullptr) {
        errorMessage = QStringLiteral("SessionOptionsAppendExecutionProvider_CUDA_V2 failed: %1")
                           .arg(QString::fromUtf8(ortApi.GetErrorMessage(status)));
        ortApi.ReleaseStatus(status);
        releaseCudaOptions();
        return false;
    }

    releaseCudaOptions();
    return true;
}

bool appendCoreMLExecutionProvider(Ort::SessionOptions &options, QString &errorMessage)
{
#if defined(__APPLE__)
    try {
        const std::unordered_map<std::string, std::string> coremlOptions = {
            { "MLComputeUnits", "ALL" },
            { "ModelFormat", "MLProgram" },
        };
        options.AppendExecutionProvider("CoreML", coremlOptions);
        return true;
    } catch (const std::exception &e) {
        errorMessage = QString::fromUtf8(e.what());
        return false;
    }
#else
    Q_UNUSED(options)
    errorMessage = QStringLiteral("CoreML execution provider is only available on macOS");
    return false;
#endif
}
}

// Public methods -----------------------------------------------------------------------------------------------------

Comb::Comb()
    : configurationSet(false)
{
}

void Comb::requestNnTransform3DCancel()
{
    nnTransform3DCancelEpoch.fetch_add(1, std::memory_order_relaxed);
}

qint32 Comb::Configuration::getLookBehind() const {
    if (dimensions == 3) {
        // In 3D mode, we need to see the previous frame
        return 1;
    }

    return 0;
}

qint32 Comb::Configuration::getLookAhead() const {
    if (dimensions == 3) {
        // nnTransform3D requires one extra future frame for overlap-add accumulation.
        return nnTransform3D ? 2 : 1;
    }

    return 0;
}

// Return the current configuration
const Comb::Configuration &Comb::getConfiguration() const {
    return configuration;
}

// Set the comb filter configuration parameters
void Comb::updateConfiguration(const LdDecodeMetaData::VideoParameters &_videoParameters, const Comb::Configuration &_configuration)
{
    // Copy the configuration parameters
    videoParameters = _videoParameters;
    configuration = _configuration;
    const qint32 frameHeight = ((videoParameters.fieldHeight * 2) - 1);

    // Range check the frame dimensions
    if (videoParameters.fieldWidth > MAX_WIDTH) qCritical() << "Comb::Comb(): Frame width exceeds allowed maximum!";
    if (frameHeight > MAX_HEIGHT) qCritical() << "Comb::Comb(): Frame height exceeds allowed maximum!";

    const qint32 clampedFirstActiveFrameLine = qBound(0, videoParameters.firstActiveFrameLine, qMax(0, frameHeight - 1));
    const qint32 clampedLastActiveFrameLine = qBound(
        clampedFirstActiveFrameLine + 1,
        videoParameters.lastActiveFrameLine,
        qMax(clampedFirstActiveFrameLine + 1, frameHeight)
    );
    if (clampedFirstActiveFrameLine != videoParameters.firstActiveFrameLine
        || clampedLastActiveFrameLine != videoParameters.lastActiveFrameLine) {
        qWarning() << "Comb::updateConfiguration(): Clamping invalid active frame line bounds from"
                   << videoParameters.firstActiveFrameLine << "-" << videoParameters.lastActiveFrameLine
                   << "to" << clampedFirstActiveFrameLine << "-" << clampedLastActiveFrameLine;
    }
    videoParameters.firstActiveFrameLine = clampedFirstActiveFrameLine;
    videoParameters.lastActiveFrameLine = clampedLastActiveFrameLine;

    const qint32 clampedActiveVideoStart = qBound(0, videoParameters.activeVideoStart, qMax(0, videoParameters.fieldWidth - 1));
    const qint32 clampedActiveVideoEnd = qBound(
        clampedActiveVideoStart + 1,
        videoParameters.activeVideoEnd,
        videoParameters.fieldWidth
    );
    if (clampedActiveVideoStart != videoParameters.activeVideoStart
        || clampedActiveVideoEnd != videoParameters.activeVideoEnd) {
        qWarning() << "Comb::updateConfiguration(): Clamping invalid active video sample bounds from"
                   << videoParameters.activeVideoStart << "-" << videoParameters.activeVideoEnd
                   << "to" << clampedActiveVideoStart << "-" << clampedActiveVideoEnd;
    }
    videoParameters.activeVideoStart = clampedActiveVideoStart;
    videoParameters.activeVideoEnd = clampedActiveVideoEnd;

    if (videoParameters.activeVideoStart < 16) {
        qWarning() << "Comb::updateConfiguration(): activeVideoStart is near frame edge; edge-safe filtering will be used";
    }

    // Check the sample rate is close to 4 * fSC.
    // Older versions of ld-decode used integer approximations, so this needs
    // to be an approximate comparison.
    if (fabs((videoParameters.sampleRate / videoParameters.fSC) - 4.0) > 1.0e-6)
    {
        qCritical() << "Data is not in 4fsc sample rate, color decoding will not work properly!";
    }

    configurationSet = true;
}

void Comb::decodeFrames(const QVector<SourceField>& inputFields, qint32 startIndex, qint32 endIndex,
    QVector<ComponentFrame>& componentFrames)
{
    assert(configurationSet);
    assert((componentFrames.size() * 2) == (endIndex - startIndex));

    if (configuration.dimensions == 3 && configuration.nnTransform3D) {
        const quint64 decodeEpoch = nnTransform3DCancelEpoch.load(std::memory_order_relaxed);
        std::map<qint32, std::shared_ptr<FrameBuffer>> frameBuffers;
        const qint32 frameCount = (endIndex - startIndex) / 2;

        auto getFrameBuffer = [&](qint32 frameIndex) -> std::shared_ptr<FrameBuffer> {
            auto existing = frameBuffers.find(frameIndex);
            if (existing != frameBuffers.end()) {
                return existing->second;
            }

            auto frameBuffer = std::make_shared<FrameBuffer>(videoParameters, configuration);
            const qint32 fieldIndex = startIndex + (frameIndex * 2);

            if (fieldIndex >= 0 && (fieldIndex + 1) < inputFields.size()) {
                frameBuffer->loadFields(inputFields[fieldIndex], inputFields[fieldIndex + 1]);
                frameBuffer->split1D();
                frameBuffer->split2D();
            }

            frameBuffers[frameIndex] = frameBuffer;
            return frameBuffer;
        };

        for (qint32 frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            auto currentFrameBuffer = getFrameBuffer(frameIndex);
            auto nextFrameBuffer = getFrameBuffer(frameIndex + 1);
            const bool nnCompleted = currentFrameBuffer->split3DnnTransform(
                *nextFrameBuffer, frameIndex, nnTransform3DCancelEpoch, decodeEpoch
            );

            componentFrames[frameIndex].init(videoParameters);
            currentFrameBuffer->setComponentFrame(componentFrames[frameIndex]);
            currentFrameBuffer->copyRawToLuma();
            if (nnCompleted) {
                currentFrameBuffer->finalizeNnTransform3D();
            } else {
                currentFrameBuffer->fallbackNnTransform3DTo2D();
            }

            if (configuration.phaseCompensation) {
                currentFrameBuffer->splitIQlocked();
            } else {
                currentFrameBuffer->splitIQ();
                currentFrameBuffer->adjustY();
            }
            currentFrameBuffer->filterIQ();
            currentFrameBuffer->doCNR();
            currentFrameBuffer->doYNR();
            currentFrameBuffer->transformIQ(configuration.chromaGain, configuration.chromaPhase);

            frameBuffers.erase(frameIndex);
        }
        return;
    }

    // Buffers for the next, current and previous frame.
    // Because we only need three of these, we allocate them upfront then
    // rotate the pointers below.
    auto nextFrameBuffer = std::make_unique<FrameBuffer>(videoParameters, configuration);
    auto currentFrameBuffer = std::make_unique<FrameBuffer>(videoParameters, configuration);
    auto previousFrameBuffer = std::make_unique<FrameBuffer>(videoParameters, configuration);

    // ================= 1. Initialize GPU filter engine =================
    bool useGpuNnTransform3D = configuration.useNNTransform3D;
#if LDCHROMA_HAS_CUDA
    std::unique_ptr<nnTransform3DCUDA> gpuFilter;
#else
    if (useGpuNnTransform3D) {
        static std::once_flag gpuUnavailableWarningOnce;
        std::call_once(gpuUnavailableWarningOnce, []() {
            qWarning() << "nnTransform3D CUDA kernel path requested but this build does not include CUDA kernels; falling back to non-CUDA path";
        });
        useGpuNnTransform3D = false;
    }
#endif
    std::vector<double> gpuOutChromaDouble;
 #if LDCHROMA_HAS_CUDA
    if (useGpuNnTransform3D) {
        gpuFilter = std::make_unique<nnTransform3DCUDA>(
            videoParameters.activeVideoStart,
            videoParameters.activeVideoEnd,
            "chroma_net.onnx"
        );
        gpuOutChromaDouble.resize(FRAME_WIDTH * FRAME_HEIGHT);
    }
 #endif
    // ==========================================================

    const qint32 preStartIndex = (configuration.dimensions == 3) ? startIndex - 4 : startIndex - 2;
    for (qint32 fieldIndex = preStartIndex; fieldIndex < endIndex; fieldIndex += 2) {
        const qint32 frameIndex = (fieldIndex - startIndex) / 2;

        // Rotate frame buffers
        {
            auto recycle = std::move(previousFrameBuffer);
            previousFrameBuffer = std::move(currentFrameBuffer);
            currentFrameBuffer = std::move(nextFrameBuffer);
            nextFrameBuffer = std::move(recycle);
        }

        if (fieldIndex + 3 < inputFields.size()) {
            nextFrameBuffer->loadFields(inputFields[fieldIndex + 2], inputFields[fieldIndex + 3]);

            // ================= 2. Feed data to the GPU =================
            if (useGpuNnTransform3D) {
#if LDCHROMA_HAS_CUDA
                gpuFilter->processFrame(nextFrameBuffer->getRawBuffer(), gpuOutChromaDouble.data());
#endif
            }
            else {
                nextFrameBuffer->split1D();
                nextFrameBuffer->split2D();
            }
            // ===================================================
        }
        else if (useGpuNnTransform3D) {
            // At end-of-file, pass nullptr to flush the GPU padding path
#if LDCHROMA_HAS_CUDA
            gpuFilter->processFrame(nullptr, gpuOutChromaDouble.data());
#endif
        }

        if (fieldIndex < startIndex) {
            continue; // LookBehind phase; skip output writes for this iteration
        }

        // ================= 3. Collect 3D chroma result =================
        if (useGpuNnTransform3D) {
            currentFrameBuffer->applyGPUChroma(gpuOutChromaDouble.data(), videoParameters.activeVideoStart, videoParameters.activeVideoEnd, videoParameters.firstActiveFrameLine, videoParameters.lastActiveFrameLine);
        }
        else if (configuration.dimensions == 3) {
            currentFrameBuffer->split3D(*previousFrameBuffer, *nextFrameBuffer);
        }
        // =======================================================

        componentFrames[frameIndex].init(videoParameters);
        currentFrameBuffer->setComponentFrame(componentFrames[frameIndex]);
        currentFrameBuffer->copyRawToLuma();

        if (configuration.phaseCompensation) {
            currentFrameBuffer->splitIQlocked();
        }
        else {
            currentFrameBuffer->splitIQ();
            currentFrameBuffer->adjustY();
        }
        currentFrameBuffer->filterIQ();
        currentFrameBuffer->doCNR();
        currentFrameBuffer->doYNR();
        currentFrameBuffer->transformIQ(configuration.chromaGain, configuration.chromaPhase);

        if (configuration.dimensions == 3 && configuration.showMap) {
            currentFrameBuffer->overlayMap(*previousFrameBuffer, *nextFrameBuffer);
        }
    }
}
// Private methods ----------------------------------------------------------------------------------------------------

Comb::FrameBuffer::FrameBuffer(const LdDecodeMetaData::VideoParameters &videoParameters_,
                               const Configuration &configuration_)
    : videoParameters(videoParameters_), configuration(configuration_)
{
    // Set the frame height
    frameHeight = ((videoParameters.fieldHeight * 2) - 1);

    // Set the IRE scale
    irescale = (videoParameters.white16bIre - videoParameters.black16bIre) / 100;

    rawbuffer.fill(videoParameters.black16bIre, frameHeight * videoParameters.fieldWidth);
    nnAccChroma = QVector<QVector<double>>(frameHeight, QVector<double>(videoParameters.fieldWidth, 0.0));
    nnWeightSum = QVector<QVector<double>>(frameHeight, QVector<double>(videoParameters.fieldWidth, 0.0));

    for (qint32 buf = 0; buf < 3; buf++) {
        for (qint32 y = 0; y < MAX_HEIGHT; y++) {
            for (qint32 x = 0; x < MAX_WIDTH; x++) {
                clpbuffer[buf].pixel[y][x] = 0.0;
            }
        }
    }
}

bool Comb::FrameBuffer::split3DnnTransform(FrameBuffer &nextFrame, qint32 frameIndex,
                                           const std::atomic<quint64> &cancelEpoch, quint64 decodeEpoch)
{
    Q_UNUSED(frameIndex)

    static constexpr qint32 Nx = 16;
    static constexpr qint32 Ny = 16;
    static constexpr qint32 Nt = 4;
    static constexpr qint32 StepX = 8;
    static constexpr qint32 StepY = 8;

    static thread_local fftw_plan forwardPlan = nullptr;
    static thread_local fftw_plan inversePlan = nullptr;
    if (!forwardPlan || !inversePlan) {
        auto *planIn = reinterpret_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex) * Nt * Ny * Nx));
        auto *planOut = reinterpret_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex) * Nt * Ny * Nx));
        forwardPlan = fftw_plan_dft_3d(Nt, Ny, Nx, planIn, planOut, FFTW_FORWARD, FFTW_ESTIMATE);
        inversePlan = fftw_plan_dft_3d(Nt, Ny, Nx, planOut, planIn, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_free(planIn);
        fftw_free(planOut);
    }

    static thread_local fftw_complex tileInput[Nt * Ny * Nx];
    static thread_local fftw_complex tileSpectrum[Nt * Ny * Nx];
    auto *in = tileInput;
    auto *out = tileSpectrum;

    static double winX[Nx];
    static double winY[Ny];
    static double winT[Nt];
    static std::once_flag windowInitOnce;
    std::call_once(windowInitOnce, []() {
        for (qint32 i = 0; i < Nx; i++) {
            winX[i] = sin(M_PI * (static_cast<double>(i) + 0.5) / static_cast<double>(Nx));
            winY[i] = winX[i];
        }
        for (qint32 i = 0; i < Nt; i++) {
            winT[i] = sin(M_PI * (static_cast<double>(i) + 0.5) / static_cast<double>(Nt));
        }
    });

    static std::once_flag onnxInitOnce;
    static bool onnxReady = false;
    static std::unique_ptr<Ort::Env> ortEnv;
    static std::unique_ptr<Ort::Session> ortSession;
    static std::mutex onnxRunMutex;
    std::call_once(onnxInitOnce, []() {
        try {
            ortEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LdDecodeToolsNnTransform3D");
            const NnExecutionProviderPreference providerPreference = getNnExecutionProviderPreference();
            const qint32 intraOpThreads = getNnIntraOpThreads();
            const QString intraOpDescription = (intraOpThreads > 0)
                ? QString::number(intraOpThreads)
                : QStringLiteral("default");

            auto configureSessionOptions = [intraOpThreads](Ort::SessionOptions &options) {
                if (intraOpThreads > 0) {
                    options.SetIntraOpNumThreads(intraOpThreads);
                }
                options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            };

            auto tryCreateSession = [&](Ort::SessionOptions &options, const QString &providerName) -> bool {
                try {
                    ortSession = std::make_unique<Ort::Session>(
                        *ortEnv,
                        static_cast<const void *>(kChromaNetV2OnnxData),
                        kChromaNetV2OnnxDataSize,
                        options
                    );
                    onnxReady = true;
                    qInfo() << "nnTransform3D ONNX provider:" << providerName
                            << "(intra-op threads:" << intraOpDescription << ")";
                    return true;
                } catch (const std::exception &e) {
                    qWarning() << "Failed to initialize nnTransform3D ONNX session with"
                               << providerName << "provider:" << e.what();
                    return false;
                }
            };

            #if defined(__APPLE__)
            const bool allowCuda = false;
            // ORT's CoreML EP currently rejects 3D Conv (deliberate gate in
            // conv_op_builder.cc), so for this model every Conv falls back to
            // CPU and CoreML is a net ~7% regression. Keep it opt-in via an
            // explicit LDDECODE_NNTRANSFORM3D_PROVIDER=coreml until that lands.
            const bool allowCoreML = providerPreference == NnExecutionProviderPreference::CoreML;
            if (providerPreference == NnExecutionProviderPreference::Cuda) {
                qWarning() << "Ignoring" << kNnProviderEnvVar
                           << "=cuda on macOS; using CPU provider";
            }
            #else
            const bool allowCuda = providerPreference != NnExecutionProviderPreference::Cpu
                                && providerPreference != NnExecutionProviderPreference::CoreML;
            const bool allowCoreML = false;
            if (providerPreference == NnExecutionProviderPreference::CoreML) {
                qWarning() << "Ignoring" << kNnProviderEnvVar
                           << "=coreml on non-Apple platforms; using CPU provider";
            }
            #endif

            if (allowCuda) {
                Ort::SessionOptions cudaOptions;
                configureSessionOptions(cudaOptions);

                QString appendError;
                if (appendCudaExecutionProvider(cudaOptions, appendError)) {
                    tryCreateSession(cudaOptions, QStringLiteral("CUDA"));
                } else {
                    qWarning() << "Unable to enable nnTransform3D CUDA provider:" << appendError;
                }
            }

            if (!onnxReady && allowCoreML) {
                Ort::SessionOptions coremlOptions;
                configureSessionOptions(coremlOptions);

                QString appendError;
                if (appendCoreMLExecutionProvider(coremlOptions, appendError)) {
                    tryCreateSession(coremlOptions, QStringLiteral("CoreML"));
                } else {
                    qWarning() << "Unable to enable nnTransform3D CoreML provider:" << appendError;
                }
            }

            if (!onnxReady) {
                Ort::SessionOptions cpuOptions;
                configureSessionOptions(cpuOptions);
                tryCreateSession(cpuOptions, QStringLiteral("CPU"));
            }

            if (!onnxReady) {
                qCritical() << "Failed to initialize nnTransform3D ONNX session using provider preference:"
                            << providerPreferenceToString(providerPreference);
            }
        } catch (const std::exception &e) {
            qCritical() << "Failed to initialize nnTransform3D ONNX runtime:" << e.what();
            onnxReady = false;
        }
    });

    if (!onnxReady || !forwardPlan || !inversePlan) {
        static std::once_flag nnUnavailableWarningOnce;
        std::call_once(nnUnavailableWarningOnce, []() {
            qCritical() << "nnTransform3D unavailable; falling back to 2D chroma";
        });
        for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
            for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
                nnAccChroma[lineNumber][h] = clpbuffer[1].pixel[lineNumber][h];
                nnWeightSum[lineNumber][h] = 1.0;
            }
        }
        return true;
    }

    static thread_local float modelInput[2 * Nt * Ny * Nx];
    static thread_local float magnitudes[Nt * Ny * Nx];
    static thread_local Ort::MemoryInfo modelMemoryInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    FrameBuffer *frames[2] = { this, &nextFrame };

    const qint32 startY = videoParameters.firstActiveFrameLine - (Ny / 2);
    const qint32 endY = videoParameters.lastActiveFrameLine;
    const qint32 startX = videoParameters.activeVideoStart - (Nx / 2);
    const qint32 endX = videoParameters.activeVideoEnd;
    qint32 tileCounter = 0;

    for (qint32 y = startY; y < endY; y += StepY) {
        for (qint32 x = startX; x < endX; x += StepX) {
            if (cancelEpoch.load(std::memory_order_relaxed) != decodeEpoch) {
                return false;
            }
            tileCounter++;
            if ((tileCounter & 0x3F) == 0) {
                if (QCoreApplication::instance() != nullptr) {
                    QCoreApplication::processEvents(
                        QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers,
                        1
                    );
                }
            }
            memset(in, 0, sizeof(fftw_complex) * Nt * Ny * Nx);

            double blockDc = 0.0;
            qint32 sampleCount = 0;

            for (qint32 frameOffset = 0; frameOffset < 2; frameOffset++) {
                for (qint32 subField = 0; subField < 2; subField++) {
                    const qint32 t = (frameOffset * 2) + subField;
                    const bool oddField = (t % 2) != 0;

                    for (qint32 dy = 0; dy < Ny; dy++) {
                        const qint32 absY = y + dy;
                        const bool yActive = absY >= videoParameters.firstActiveFrameLine
                                             && absY < videoParameters.lastActiveFrameLine;
                        const bool oddLine = (absY % 2) != 0;
                        if (!yActive || (oddLine != oddField)) {
                            continue;
                        }

                        const quint16 *lineData = frames[frameOffset]->rawbuffer.constData()
                                                  + (absY * videoParameters.fieldWidth);
                        for (qint32 dx = 0; dx < Nx; dx++) {
                            const qint32 absX = x + dx;
                            const bool xActive = absX >= videoParameters.activeVideoStart
                                                 && absX < videoParameters.activeVideoEnd;
                            if (!xActive) {
                                continue;
                            }

                            const qint32 index = IDX3(t, dy, dx, Nt, Ny, Nx);
                            const double value = static_cast<double>(lineData[absX]);
                            in[index][0] = value;
                            in[index][1] = 0.0;
                            blockDc += value;
                            sampleCount++;
                        }
                    }
                }
            }

            if (sampleCount == 0) {
                continue;
            }

            blockDc /= static_cast<double>(sampleCount);

            for (qint32 t = 0; t < Nt; t++) {
                const bool oddField = (t % 2) != 0;
                for (qint32 dy = 0; dy < Ny; dy++) {
                    const qint32 absY = y + dy;
                    const bool yActive = absY >= videoParameters.firstActiveFrameLine
                                         && absY < videoParameters.lastActiveFrameLine;
                    const bool oddLine = (absY % 2) != 0;
                    for (qint32 dx = 0; dx < Nx; dx++) {
                        const qint32 absX = x + dx;
                        const bool xActive = absX >= videoParameters.activeVideoStart
                                             && absX < videoParameters.activeVideoEnd;
                        const qint32 index = IDX3(t, dy, dx, Nt, Ny, Nx);

                        if (yActive && xActive && (oddLine == oddField)) {
                            in[index][0] = (in[index][0] - blockDc) * winT[t] * winY[dy] * winX[dx];
                        } else {
                            in[index][0] = 0.0;
                            in[index][1] = 0.0;
                        }
                    }
                }
            }

            fftw_execute_dft(forwardPlan, in, out);

            for (qint32 i = 0; i < Nt * Ny * Nx; i++) {
                magnitudes[i] = sqrtf(static_cast<float>((out[i][0] * out[i][0]) + (out[i][1] * out[i][1])));
            }
            memcpy(modelInput, magnitudes, sizeof(float) * Nt * Ny * Nx);

            qint32 reflectedIndex = Nt * Ny * Nx;
            for (qint32 ft = 0; ft < Nt; ft++) {
                const qint32 refT = ((2 - ft) % 4 + 4) % 4;
                for (qint32 fy = 0; fy < Ny; fy++) {
                    const qint32 refY = (Ny - fy) % Ny;
                    for (qint32 fx = 0; fx < Nx; fx++) {
                        const qint32 refX = ((Nx / 2) - fx + Nx) % Nx;
                        modelInput[reflectedIndex++] = magnitudes[IDX3(refT, refY, refX, Nt, Ny, Nx)];
                    }
                }
            }

            static const int64_t inputShape[] = { 1, 2, 4, 16, 16 };
            Ort::Value modelInputTensor = Ort::Value::CreateTensor<float>(
                modelMemoryInfo, modelInput, 2 * Nt * Ny * Nx, inputShape, 5
            );
            static const char *inputNames[] = { "input" };
            static const char *outputNames[] = { "output" };
            std::vector<Ort::Value> modelOutputs;
            {
                std::lock_guard<std::mutex> runLock(onnxRunMutex);
                if (!onnxReady || !ortSession) {
                    return false;
                }
                try {
                    modelOutputs = ortSession->Run(
                        Ort::RunOptions{ nullptr },
                        inputNames, &modelInputTensor, 1,
                        outputNames, 1
                    );
                } catch (const Ort::Exception &e) {
                    qWarning() << "nnTransform3D ONNX inference failed; disabling nn session and falling back to 2D chroma:"
                               << e.what();
                    onnxReady = false;
                    ortSession.reset();
                    return false;
                }
            }
            if (cancelEpoch.load(std::memory_order_relaxed) != decodeEpoch) {
                return false;
            }
            const float *mask = modelOutputs[0].GetTensorData<float>();
            for (qint32 i = 0; i < Nt * Ny * Nx; i++) {
                out[i][0] *= mask[i];
                out[i][1] *= mask[i];
            }

            fftw_execute_dft(inversePlan, out, in);

            for (qint32 t = 0; t < Nt; t++) {
                const qint32 targetFrameIndex = t / 2;
                const bool oddField = (t % 2) != 0;
                FrameBuffer *targetFrame = frames[targetFrameIndex];

                for (qint32 dy = 0; dy < Ny; dy++) {
                    const qint32 absY = y + dy;
                    if (absY < videoParameters.firstActiveFrameLine || absY >= videoParameters.lastActiveFrameLine) {
                        continue;
                    }
                    if ((absY % 2 != 0) != oddField) {
                        continue;
                    }

                    for (qint32 dx = 0; dx < Nx; dx++) {
                        const qint32 absX = x + dx;
                        if (absX < videoParameters.activeVideoStart || absX >= videoParameters.activeVideoEnd) {
                            continue;
                        }
                        const qint32 index = IDX3(t, dy, dx, Nt, Ny, Nx);
                        const double value = in[index][0] / static_cast<double>(Nt * Ny * Nx);
                        const double weight = winT[t] * winY[dy] * winX[dx];

                        targetFrame->nnAccChroma[absY][absX] += value * weight;
                        targetFrame->nnWeightSum[absY][absX] += weight * weight;
                    }
                }
            }
        }
    }
    return true;
}

void Comb::FrameBuffer::finalizeNnTransform3D()
{
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            const double weight = nnWeightSum[lineNumber][h];
            clpbuffer[2].pixel[lineNumber][h] = (weight > 1.0e-6) ? (nnAccChroma[lineNumber][h] / weight) : 0.0;
        }
    }
}

void Comb::FrameBuffer::fallbackNnTransform3DTo2D()
{
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            clpbuffer[2].pixel[lineNumber][h] = clpbuffer[1].pixel[lineNumber][h];
        }
    }
}

/*
 * The color burst frequency is 227.5 cycles per line, so it flips 180 degrees for each line.
 *
 * The color burst *signal* is at 180 degrees, which is a greenish yellow.
 *
 * When SCH phase is 0 (properly aligned) the color burst is in phase with the leading edge of the HSYNC pulse.
 *
 * Per RS-170 note 6, Fields 1 and 4 have positive/rising burst phase at that point on even (1-based!) lines.
 * The color burst signal should begin exactly 19 cycles later.
 *
 * getLinePhase returns true if the color burst is rising at the leading edge.
 */

inline qint32 Comb::FrameBuffer::getFieldID(qint32 lineNumber) const
{
    bool isFirstField = ((lineNumber % 2) == 0);

    return isFirstField ? firstFieldPhaseID : secondFieldPhaseID;
}

// NOTE:  lineNumber is presumed to be starting at 1.  (This lines up with how splitIQ calls it)
inline bool Comb::FrameBuffer::getLinePhase(qint32 lineNumber) const
{
    qint32 fieldID = getFieldID(lineNumber);
    bool isPositivePhaseOnEvenLines = (fieldID == 1) || (fieldID == 4);

    int fieldLine = (lineNumber / 2);
    bool isEvenLine = (fieldLine % 2) == 0;

    return isEvenLine ? isPositivePhaseOnEvenLines : !isPositivePhaseOnEvenLines;
}

// Interlace two source fields into the framebuffer.
void Comb::FrameBuffer::loadFields(const SourceField &firstField, const SourceField &secondField)
{
    // Interlace the input fields and place in the frame buffer
    qint32 fieldLine = 0;
    rawbuffer.clear();
    for (qint32 frameLine = 0; frameLine < frameHeight; frameLine += 2) {
        rawbuffer.append(firstField.data.mid(fieldLine * videoParameters.fieldWidth, videoParameters.fieldWidth));
        rawbuffer.append(secondField.data.mid(fieldLine * videoParameters.fieldWidth, videoParameters.fieldWidth));
        fieldLine++;
    }

    // Set the phase IDs for the frame
    firstFieldPhaseID = firstField.field.fieldPhaseID;
    secondFieldPhaseID = secondField.field.fieldPhaseID;

    // Clear clpbuffer
    for (qint32 buf = 0; buf < 3; buf++) {
        for (qint32 y = 0; y < MAX_HEIGHT; y++) {
            for (qint32 x = 0; x < MAX_WIDTH; x++) {
                clpbuffer[buf].pixel[y][x] = 0.0;
            }
        }
    }

    for (qint32 line = 0; line < frameHeight; line++) {
        if (line < nnAccChroma.size()) {
            std::fill(nnAccChroma[line].begin(), nnAccChroma[line].end(), 0.0);
        }
        if (line < nnWeightSum.size()) {
            std::fill(nnWeightSum[line].begin(), nnWeightSum[line].end(), 0.0);
        }
    }

    // No component frame yet
    componentFrame = nullptr;
}

void Comb::FrameBuffer::copyRawToLuma()
{
    if (componentFrame == nullptr) {
        return;
    }

    const qint32 width = videoParameters.fieldWidth;
    for (qint32 lineNumber = 0; lineNumber < frameHeight; lineNumber++) {
        const quint16 *inLine = rawbuffer.data() + (lineNumber * width);
        double *outLine = componentFrame->y(lineNumber);

        for (qint32 h = 0; h < width; h++) {
            outLine[h] = inLine[h];
        }
    }
}

// Extract chroma into clpbuffer[0] using a 1D bandpass filter.
//
// The filter is [-0.25, 0, 0.5, 0, -0.25], a gentle bandpass centred on fSC.
// So the output will contain all of the chroma signal, but also whatever luma
// components ended up in the same frequency range.
//
// This also acts as an alias removal pre-filter for the quadrature detector in
// splitIQ, so we use its result for split2D rather than the raw signal.
void Comb::FrameBuffer::split1D()
{
    const qint32 startSample = qMax(videoParameters.activeVideoStart, 2);
    const qint32 endSample = qMin(videoParameters.activeVideoEnd, videoParameters.fieldWidth - 2);
    if (startSample >= endSample) {
        return;
    }
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line's data
        const quint16 *line = rawbuffer.data() + (lineNumber * videoParameters.fieldWidth);
        for (qint32 h = startSample; h < endSample; h++) {
            double tc1 = (line[h] - ((line[h - 2] + line[h + 2]) / 2.0)) / 2.0;

            // Record the 1D C value
            clpbuffer[0].pixel[lineNumber][h] = tc1;
        }
    }
}

// Extract chroma into clpbuffer[1] using a 2D 3-line adaptive filter.
//
// Because the phase of the chroma signal changes by 180 degrees from line to
// line, subtracting two adjacent lines that contain the same information will
// give you just the chroma signal. But real images don't necessarily contain
// the same information on every line.
//
// The "3-line adaptive" part means that we look at both surrounding lines to
// estimate how similar they are to this one. We can then compute the 2D chroma
// value as a blend of the two differences, weighted by similarity.
void Comb::FrameBuffer::split2D()
{
    // Dummy black line
    static constexpr double blackLine[MAX_WIDTH] = {0};
    const qint32 startSample = qMax(videoParameters.activeVideoStart, 1);
    const qint32 endSample = qMin(videoParameters.activeVideoEnd, videoParameters.fieldWidth - 1);
    if (startSample >= endSample) {
        return;
    }

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get pointers to the surrounding lines of 1D chroma.
        // If a line we need is outside the active area, use blackLine instead.
        const double *previousLine = blackLine;
        if (lineNumber - 2 >= videoParameters.firstActiveFrameLine) {
            previousLine = clpbuffer[0].pixel[lineNumber - 2];
        }
        const double *currentLine = clpbuffer[0].pixel[lineNumber];
        const double *nextLine = blackLine;
        if (lineNumber + 2 < videoParameters.lastActiveFrameLine) {
            nextLine = clpbuffer[0].pixel[lineNumber + 2];
        }

        for (qint32 h = startSample; h < endSample; h++) {
            double kp, kn;

            // Summing the differences of the *absolute* values of the 1D chroma samples
            // will give us a low value if the two lines are nearly in phase (strong Y)
            // or nearly 180 degrees out of phase (strong C) -- i.e. the two cases where
            // the 2D filter is probably usable. Also give a small bonus if
            // there's a large signal (we think).
            kp  = fabs(fabs(currentLine[h]) - fabs(previousLine[h]));
            kp += fabs(fabs(currentLine[h - 1]) - fabs(previousLine[h - 1]));
            kp -= (fabs(currentLine[h]) + fabs(previousLine[h - 1])) * .10;
            kn  = fabs(fabs(currentLine[h]) - fabs(nextLine[h]));
            kn += fabs(fabs(currentLine[h - 1]) - fabs(nextLine[h - 1]));
            kn -= (fabs(currentLine[h]) + fabs(nextLine[h - 1])) * .10;

            // Map the difference into a weighting 0-1.
            // 1 means in phase or unknown; 0 means out of phase (more than kRange difference).
            const double kRange = 45 * irescale;
            kp = qBound(0.0, 1 - (kp / kRange), 1.0);
            kn = qBound(0.0, 1 - (kn / kRange), 1.0);

            double sc = 1.0;

            if ((kn > 0) || (kp > 0)) {
                // At least one of the next/previous lines has a good phase relationship.

                // If one of them is much better than the other, only use that one
                if (kn > (3 * kp)) kp = 0;
                else if (kp > (3 * kn)) kn = 0;

                sc = (2.0 / (kn + kp));
                if (sc < 1.0) sc = 1.0;
            } else {
                // Neither line has a good phase relationship.

                // But are they similar to each other? If so, we can use both of them!
                if ((fabs(fabs(previousLine[h]) - fabs(nextLine[h])) - fabs((nextLine[h] + previousLine[h]) * .2)) <= 0) {
                    kn = kp = 1;
                }

                // Else kn = kp = 0, so we won't extract any chroma for this sample.
                // (Some NTSC decoders fall back to the 1D chroma in this situation.)
            }

            // Compute the weighted sum of differences, giving the 2D chroma value
            double tc1;
            tc1  = ((currentLine[h] - previousLine[h]) * kp * sc);
            tc1 += ((currentLine[h] - nextLine[h]) * kn * sc);
            tc1 /= 4;

            clpbuffer[1].pixel[lineNumber][h] = tc1;
        }
    }
}

// Extract chroma into clpbuffer[2] using an adaptive 3D filter.
//
// For each sample, this builds a list of candidates from other positions that
// should have a 180 degree phase relationship to the current sample, and look
// like they have similar luma/chroma content. It then picks the most similar
// candidate.
void Comb::FrameBuffer::split3D(const FrameBuffer &previousFrame, const FrameBuffer &nextFrame)
{
    const qint32 startSample = qMax(videoParameters.activeVideoStart, 1);
    const qint32 endSample = qMin(videoParameters.activeVideoEnd, videoParameters.fieldWidth - 1);
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            clpbuffer[2].pixel[lineNumber][h] = clpbuffer[1].pixel[lineNumber][h];
        }

        if (startSample >= endSample) {
            continue;
        }

        for (qint32 h = startSample; h < endSample; h++) {
            // Select the best candidate
            qint32 bestIndex;
            double bestSample;
            getBestCandidate(lineNumber, h, previousFrame, nextFrame, bestIndex, bestSample);
            if (bestIndex >= CAND_PREV_FIELD) {
                // Compute a 3D result.
                // This sample is Y + C; the candidate is (ideally) Y - C. So compute C as ((Y + C) - (Y - C)) / 2.
                clpbuffer[2].pixel[lineNumber][h] = (clpbuffer[0].pixel[lineNumber][h] - bestSample) / 2;
            }
        }
    }
}

// Evaluate all candidates for 3D decoding for a given position, and return the best one
void Comb::FrameBuffer::getBestCandidate(qint32 lineNumber, qint32 h,
                                         const FrameBuffer &previousFrame, const FrameBuffer &nextFrame,
                                         qint32 &bestIndex, double &bestSample) const
{
    Candidate candidates[8];

    // Bias the comparison so that we prefer 3D results, then 2D, then 1D
    // adaptThreshold scales these bonuses: higher = stronger 3D preference
    const double LINE_BONUS = -2.0 * configuration.adaptThreshold;
    const double FIELD_BONUS = LINE_BONUS - (2.0 * configuration.adaptThreshold);
    const double FRAME_BONUS = FIELD_BONUS - (2.0 * configuration.adaptThreshold);

    // 1D: Same line, 2 samples left and right
    candidates[CAND_LEFT]  = getCandidate(lineNumber, h, *this, lineNumber, h - 2, 0);
    candidates[CAND_RIGHT] = getCandidate(lineNumber, h, *this, lineNumber, h + 2, 0);

    // 2D: Same field, 1 line up and down
    candidates[CAND_UP]   = getCandidate(lineNumber, h, *this, lineNumber - 2, h, LINE_BONUS);
    candidates[CAND_DOWN] = getCandidate(lineNumber, h, *this, lineNumber + 2, h, LINE_BONUS);

    // Immediately adjacent lines in previous/next field
    if (getLinePhase(lineNumber) == getLinePhase(lineNumber - 1)) {
        candidates[CAND_PREV_FIELD] = getCandidate(lineNumber, h, previousFrame, lineNumber - 1, h, FIELD_BONUS);
        candidates[CAND_NEXT_FIELD] = getCandidate(lineNumber, h, *this, lineNumber + 1, h, FIELD_BONUS);
    } else {
        candidates[CAND_PREV_FIELD] = getCandidate(lineNumber, h, *this, lineNumber - 1, h, FIELD_BONUS);
        candidates[CAND_NEXT_FIELD] = getCandidate(lineNumber, h, nextFrame, lineNumber + 1, h, FIELD_BONUS);
    }

    // Previous/next frame, same position
    candidates[CAND_PREV_FRAME] = getCandidate(lineNumber, h, previousFrame, lineNumber, h, FRAME_BONUS);
    candidates[CAND_NEXT_FRAME] = getCandidate(lineNumber, h, nextFrame, lineNumber, h, FRAME_BONUS);

    if (configuration.adaptive) {
        // Find the candidate with the lowest penalty
        bestIndex = 0;
        for (qint32 i = 1; i < NUM_CANDIDATES; i++) {
            if (candidates[i].penalty < candidates[bestIndex].penalty) bestIndex = i;
        }
    } else {
        // Adaptive mode is disabled - do 3D against the previous frame
        bestIndex = CAND_PREV_FRAME;
    }

    bestSample = candidates[bestIndex].sample;
}

// Evaluate a candidate for 3D decoding
Comb::FrameBuffer::Candidate Comb::FrameBuffer::getCandidate(qint32 refLineNumber, qint32 refH,
                                                             const FrameBuffer &frameBuffer, qint32 lineNumber, qint32 h,
                                                             double adjustPenalty) const
{
    Candidate result;
    const bool lineOutOfRange = (lineNumber < videoParameters.firstActiveFrameLine)
        || (lineNumber >= videoParameters.lastActiveFrameLine)
        || (refLineNumber < videoParameters.firstActiveFrameLine)
        || (refLineNumber >= videoParameters.lastActiveFrameLine);
    const bool sampleOutOfRange = (h < 1)
        || (h >= (videoParameters.fieldWidth - 1))
        || (refH < 1)
        || (refH >= (videoParameters.fieldWidth - 1));
    if (lineOutOfRange || sampleOutOfRange) {
        result.sample = 0.0;
        result.penalty = 1000.0;
        return result;
    }
    result.sample = frameBuffer.clpbuffer[0].pixel[lineNumber][h];

    // The target sample should have 180 degrees phase difference from the reference.
    // If it doesn't (e.g. because it's a blank frame or the player skipped), it's not viable.
    const qint32 wantPhase = (2 + (getLinePhase(refLineNumber) ? 2 : 0) + refH) % 4;
    const qint32 havePhase = ((frameBuffer.getLinePhase(lineNumber) ? 2 : 0) + h) % 4;
    if (wantPhase != havePhase) {
        result.penalty = 1000.0;
        return result;
    }

    // Pointers to the baseband data
    const quint16 *refLine = rawbuffer.data() + (refLineNumber * videoParameters.fieldWidth);
    const quint16 *candidateLine = frameBuffer.rawbuffer.data() + (lineNumber * videoParameters.fieldWidth);

    // Penalty based on mean luma difference in IRE over surrounding three samples
    double yPenalty = 0.0;
    for (qint32 offset = -1; offset < 2; offset++) {
        const double refC = clpbuffer[1].pixel[refLineNumber][refH + offset];
        const double refY = refLine[refH + offset] - refC;

        const double candidateC = frameBuffer.clpbuffer[1].pixel[lineNumber][h + offset];
        const double candidateY = candidateLine[h + offset] - candidateC;

        yPenalty += fabs(refY - candidateY);
    }
    yPenalty = yPenalty / 3 / irescale;

    // Penalty based on mean I/Q difference in IRE over surrounding three samples
    double iqPenalty = 0.0;
    for (qint32 offset = -1; offset < 2; offset++) {
        // The reference and candidate are 180 degrees out of phase here, so negate one
        const double refC = clpbuffer[1].pixel[refLineNumber][refH + offset];
        const double candidateC = -frameBuffer.clpbuffer[1].pixel[lineNumber][h + offset];

        // I and Q samples alternate, so weight the two channels equally
        static constexpr double weights[] = {0.5, 1.0, 0.5};
        iqPenalty += fabs(refC - candidateC) * weights[offset + 1];
    }
    // Weaken this relative to luma, to avoid spurious colour in the 2D result from showing through
    iqPenalty = (iqPenalty / 2 / irescale) * 0.28;

    result.penalty = yPenalty + iqPenalty + adjustPenalty * configuration.chromaWeight;
    return result;
}

namespace {
    // Information about a line we're decoding.
    struct BurstInfo {
        double bsin, bcos;
    };

    // Rotate the burst angle to get the correct values.
    // We do the 33 degree rotation here to avoid computing it for every pixel.
    constexpr double ROTATE_SIN = 0.5446390350150271;
    constexpr double ROTATE_COS = 0.838670567945424;

    BurstInfo detectBurst(const quint16* lineData,
                          const LdDecodeMetaData::VideoParameters& videoParameters)
    {
        double bsin = 0, bcos = 0;

        // Find absolute burst phase relative to the reference carrier by
        // product detection.
        // For now we just use the burst on the current line, but we could possibly do some averaging with
        // neighbouring lines later if needed.
        for (qint32 i = videoParameters.colourBurstStart; i < videoParameters.colourBurstEnd; i++) {
            bsin += lineData[i] * sin4fsc(i);
            bcos += lineData[i] * cos4fsc(i);
        }

        // Normalise the sums above
        const qint32 colourBurstLength = videoParameters.colourBurstEnd - videoParameters.colourBurstStart;
        bsin /= colourBurstLength;
        bcos /= colourBurstLength;

        const double burstNorm = qMax(sqrt(bsin * bsin + bcos * bcos), 130000.0 / 128);

        bsin /= burstNorm;
        bcos /= burstNorm;

        const BurstInfo info{bsin, bcos};
        return info;
    }
}


void Comb::FrameBuffer::applyGPUChroma(const double* gpuOutChromaDouble, qint32 activeStart, qint32 activeEnd, qint32 firstLine, qint32 lastLine) {
    for (qint32 lineNumber = firstLine; lineNumber < lastLine; lineNumber++) {
        for (qint32 h = activeStart; h < activeEnd; h++) {
            clpbuffer[2].pixel[lineNumber][h] = gpuOutChromaDouble[lineNumber * 910 + h];
        }
    }
}
#if LDCHROMA_HAS_CUDA


nnTransform3DCUDA::nnTransform3DCUDA(int activeStart, int activeEnd, const char* modelPath)
    : activeStartX(activeStart), activeEndX(activeEnd) {
    // 1. Initialize ONNX Runtime (TensorRT + CUDA fallback)
    
    env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "NTSC3D");
    Ort::SessionOptions session_options;

    try {
        OrtTensorRTProviderOptions trt_options{};
        trt_options.device_id = 0;
        trt_options.trt_fp16_enable = 1;
        trt_options.trt_engine_cache_enable = 1;
        trt_options.trt_engine_cache_path = "./trt_cache";
        session_options.AppendExecutionProvider_TensorRT(trt_options);

        OrtCUDAProviderOptions cuda_options;
        cuda_options.device_id = 0;
        session_options.AppendExecutionProvider_CUDA(cuda_options);
    }
    catch (...) {
        std::cerr << "Fallback to CPU or basic CUDA.\n";
    }

    session = std::make_unique<Ort::Session>(*env, ORT_TSTR("chroma_net.onnx"), session_options);

    // 2. Allocate persistent device memory for the previous/next frame buffers
    allocateFrameBuffer(frame0);
    allocateFrameBuffer(frame1);
}

nnTransform3DCUDA::~nnTransform3DCUDA() {
    freeGPUResources();
    // Release frame-buffer device memory
    if (frame0.d_cvbs) { cudaFree(frame0.d_cvbs); cudaFree(frame0.d_accChroma); cudaFree(frame0.d_weightSum); }
    if (frame1.d_cvbs) { cudaFree(frame1.d_cvbs); cudaFree(frame1.d_accChroma); cudaFree(frame1.d_weightSum); }
}

void nnTransform3DCUDA::allocateFrameBuffer(InternalFrame& f) {
    size_t pixels = FRAME_WIDTH * FRAME_HEIGHT;
    f.cvbs.resize(pixels, 0);
    f.h_accChroma.resize(pixels, 0.0);
    f.h_weightSum.resize(pixels, 0.0);
    cudaMalloc((void**)&f.d_cvbs, pixels * sizeof(uint16_t));
    cudaMalloc((void**)&f.d_accChroma, pixels * sizeof(double));
    cudaMalloc((void**)&f.d_weightSum, pixels * sizeof(double));
}

void nnTransform3DCUDA::resetFrameOLA(InternalFrame& f) {
    f.isPadding = false;
    size_t bytes = FRAME_WIDTH * FRAME_HEIGHT * sizeof(double);
    cudaMemset(f.d_accChroma, 0, bytes);
    cudaMemset(f.d_weightSum, 0, bytes);
}

void nnTransform3DCUDA::initGPUResources(int num_blocks) {
    if (cached_num_blocks == num_blocks) return;
    freeGPUResources();

    size_t block_size = Nt * Ny * Nx;
    size_t bytes = sizeof(cufftDoubleComplex) * num_blocks * block_size;

    cudaMalloc((void**)&d_in_batch, bytes);
    cudaMalloc((void**)&d_out_batch, bytes);

    int n[] = { Nt, Ny, Nx };
    cufftPlanMany(&p_fwd, 3, n, NULL, 1, block_size, NULL, 1, block_size, CUFFT_Z2Z, num_blocks);
    cufftPlanMany(&p_inv, 3, n, NULL, 1, block_size, NULL, 1, block_size, CUFFT_Z2Z, num_blocks);
    is_plan_created = true;

    cudaMalloc((void**)&d_ledger_y, num_blocks * sizeof(int));
    cudaMalloc((void**)&d_ledger_x, num_blocks * sizeof(int));
    cudaMalloc((void**)&d_ledger_dc, num_blocks * sizeof(double));
    cudaMalloc((void**)&d_winX, Nx * sizeof(double));
    cudaMalloc((void**)&d_winY, Ny * sizeof(double));
    cudaMalloc((void**)&d_winT, Nt * sizeof(double));

    cudaMalloc((void**)&d_trt_input, num_blocks * 2 * block_size * sizeof(float));
    cudaMalloc((void**)&d_mask, num_blocks * block_size * sizeof(float));

    cached_num_blocks = num_blocks;

    // Initialize window functions and upload them to the GPU
    std::vector<double> winX(Nx), winY(Ny), winT(Nt);
    for (int i = 0; i < Nx; ++i) winX[i] = sin(M_PI * (i + 0.5) / Nx);
    for (int i = 0; i < Ny; ++i) winY[i] = sin(M_PI * (i + 0.5) / Ny);
    for (int i = 0; i < Nt; ++i) winT[i] = sin(M_PI * (i + 0.5) / Nt);
    cudaMemcpy(d_winX, winX.data(), Nx * sizeof(double), cudaMemcpyHostToDevice);
    cudaMemcpy(d_winY, winY.data(), Ny * sizeof(double), cudaMemcpyHostToDevice);
    cudaMemcpy(d_winT, winT.data(), Nt * sizeof(double), cudaMemcpyHostToDevice);
}

void nnTransform3DCUDA::freeGPUResources() {
    if (d_in_batch) { cudaFree(d_in_batch); d_in_batch = nullptr; }
    if (d_out_batch) { cudaFree(d_out_batch); d_out_batch = nullptr; }
    if (d_trt_input) { cudaFree(d_trt_input); d_trt_input = nullptr; }
    if (d_mask) { cudaFree(d_mask); d_mask = nullptr; }
    if (d_ledger_y) { cudaFree(d_ledger_y); cudaFree(d_ledger_x); cudaFree(d_ledger_dc); }
    if (d_winX) { cudaFree(d_winX); cudaFree(d_winY); cudaFree(d_winT); }
    if (is_plan_created) { cufftDestroy(p_fwd); cufftDestroy(p_inv); is_plan_created = false; }
    cached_num_blocks = 0;
}

// Called once per frame by the video pipeline
void nnTransform3DCUDA::processFrame(const uint16_t* inputFrame, double* outChromaDouble) {
    size_t frameBytes = FRAME_WIDTH * FRAME_HEIGHT * sizeof(uint16_t);

    if (inputFrame == nullptr) {
        std::swap(frame0, frame1);
        resetFrameOLA(frame1);
        frame1.isPadding = true; // Force the next frame to be a zero-padding frame
    }
    
    else if (isFirstFrame) {
        frame0.isPadding = true;
        cudaMemcpy(frame1.d_cvbs, inputFrame, frameBytes, cudaMemcpyHostToDevice);
        frame1.isPadding = false;

        // Compute block count on first frame and initialize resources
        int startY_loop = 40 - (Ny / 2);
        int startX_loop = activeStartX - (Nx / 2);
        int num_blocks = 0;
        std::vector<int> h_ledger_y, h_ledger_x;
        for (int y = startY_loop; y < 525; y += 8) {
            for (int x = startX_loop; x < activeEndX; x += 8) {
                h_ledger_y.push_back(y); h_ledger_x.push_back(x);
                num_blocks++;
            }
        }
        initGPUResources(num_blocks);
        cudaMemcpy(d_ledger_y, h_ledger_y.data(), num_blocks * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(d_ledger_x, h_ledger_x.data(), num_blocks * sizeof(int), cudaMemcpyHostToDevice);
        isFirstFrame = false;
    }
    else {
        std::swap(frame0, frame1);
        resetFrameOLA(frame1);
        cudaMemcpy(frame1.d_cvbs, inputFrame, frameBytes, cudaMemcpyHostToDevice);
    }

    size_t block_size = Nt * Ny * Nx;

    launch_nnTransform3D_CUDA(
        frame0.d_cvbs, frame1.d_cvbs, frame0.isPadding, frame1.isPadding,
        frame0.d_accChroma, frame1.d_accChroma, frame0.d_weightSum, frame1.d_weightSum,
        p_fwd, p_inv, d_in_batch, d_out_batch, d_trt_input, d_mask,
        d_ledger_y, d_ledger_x, d_ledger_dc, d_winX, d_winY, d_winT,
        cached_num_blocks, block_size, activeStartX, activeEndX, session.get()
    );

    cudaMemcpy(frame0.h_accChroma.data(), frame0.d_accChroma, FRAME_WIDTH * FRAME_HEIGHT * sizeof(double), cudaMemcpyDeviceToHost);
    cudaMemcpy(frame0.h_weightSum.data(), frame0.d_weightSum, FRAME_WIDTH * FRAME_HEIGHT * sizeof(double), cudaMemcpyDeviceToHost);

  
    for (int i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; ++i) {
        int x = i % FRAME_WIDTH;
        if (x < activeStartX || x >= activeEndX || frame0.isPadding) {
            outChromaDouble[i] = 0.0; 
        }
        else {
            outChromaDouble[i] = (frame0.h_weightSum[i] > 0.00001) ? (frame0.h_accChroma[i] / frame0.h_weightSum[i]) : 0.0;
        }
    }
}
#endif






// Split I and Q, taking burst phase into account.
void Comb::FrameBuffer::splitIQlocked()
{
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line's data
        const quint16 *line = rawbuffer.data() + (lineNumber * videoParameters.fieldWidth);
        // Calculate burst phase
        const auto info = detectBurst(line, videoParameters);

        double *Y = componentFrame->y(lineNumber);
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);
        const qint32 startSample = qMax(0, videoParameters.activeVideoStart);
        const qint32 endSample = qMin(videoParameters.activeVideoEnd, videoParameters.fieldWidth);
        const qint32 shiftedWriteEndSample = qMin(endSample, videoParameters.fieldWidth - 1);

        for (qint32 h = startSample; h < shiftedWriteEndSample; h++) {
            const auto val = clpbuffer[configuration.dimensions - 1].pixel[lineNumber][h];

            // Demodulate the sine and cosine components.
            const auto lsin = val * sin4fsc(h) * 2;
            const auto lcos = val * cos4fsc(h) * 2;
            // Rotate the demodulated vector by the burst phase.
            const auto ti = (lsin * info.bcos - lcos * info.bsin);
            const auto tq = (lsin * info.bsin + lcos * info.bcos);

            // Invert Q and rotate to get the correct I/Q vector.
            // TODO: Needed to shift the chroma 1 sample to the right to get it to line up
            // may not get the first pixel in each line correct because of this.
            I[h + 1] = ti * ROTATE_COS - tq * -ROTATE_SIN;
            Q[h + 1] = -(ti * -ROTATE_SIN + tq * ROTATE_COS);
            // Subtract the split chroma part from the luma signal.
            Y[h] = line[h] - val;
        }

        for (qint32 h = shiftedWriteEndSample; h < endSample; h++) {
            const auto val = clpbuffer[configuration.dimensions - 1].pixel[lineNumber][h];
            Y[h] = line[h] - val;
        }
    }
}

// Spilt the I and Q
void Comb::FrameBuffer::splitIQ()
{
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line's data
        const quint16 *line = rawbuffer.data() + (lineNumber * videoParameters.fieldWidth);

        double *Y = componentFrame->y(lineNumber);
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);

        bool linePhase = getLinePhase(lineNumber);

        double si = 0, sq = 0;
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            qint32 phase = h % 4;

            double cavg = clpbuffer[configuration.dimensions - 1].pixel[lineNumber][h];

            if (linePhase) cavg = -cavg;

            switch (phase) {
                case 0: sq = cavg; break;
                case 1: si = -cavg; break;
                case 2: sq = -cavg; break;
                case 3: si = cavg; break;
                default: break;
            }

            Y[h] = line[h];
            I[h] = si;
            Q[h] = sq;
        }
    }
}

// Filter the IQ from the component frame
void Comb::FrameBuffer::filterIQ()
{
    auto iqFilter = makeFIRFilter(c_colorlp_b);

    // Temporary output buffer for the filter
    const int width = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    std::vector<double> tempBuf(width);

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *I = componentFrame->u(lineNumber) + videoParameters.activeVideoStart;
        double *Q = componentFrame->v(lineNumber) + videoParameters.activeVideoStart;

        // Apply filter to I
        iqFilter.apply(I, tempBuf.data(), width);
        std::copy(tempBuf.begin(), tempBuf.end(), I);

        // Apply filter to Q
        iqFilter.apply(Q, tempBuf.data(), width);
        std::copy(tempBuf.begin(), tempBuf.end(), Q);
    }
}

// Remove the colour data from the baseband (Y)
void Comb::FrameBuffer::adjustY()
{
    // remove color data from baseband (Y)
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *Y = componentFrame->y(lineNumber);
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);

        bool linePhase = getLinePhase(lineNumber);

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            double comp = 0;
            qint32 phase = h % 4;

            switch (phase) {
                case 0: comp = -Q[h]; break;
                case 1: comp = I[h]; break;
                case 2: comp = Q[h]; break;
                case 3: comp = -I[h]; break;
                default: break;
            }

            if (!linePhase) comp = -comp;
            Y[h] -= comp;
        }
    }
}

/*
 * This applies an FIR coring filter to both I and Q color channels.  It's a simple (crude?) NR technique used
 * by LD players, but effective especially on the Y/luma channel.
 *
 * A coring filter removes high frequency components (.4mhz chroma, 2.8mhz luma) of a signal up to a certain point,
 * which removes small high frequency noise.
 */

void Comb::FrameBuffer::doCNR()
{
    if (configuration.cNRLevel == 0) return;

    // nr_c is the coring level
    const double nr_c = configuration.cNRLevel * irescale;

    // High-pass filters for I/Q
    auto iFilter(f_nrc);
    auto qFilter(f_nrc);

    // Filter delay (since it's a symmetric FIR filter)
    const qint32 delay = c_nrc_b.size() / 2;

    // High-pass result
    // TODO: Cache arrays instead of reallocating every field.
    std::vector<double> hpI(videoParameters.activeVideoEnd + delay);
    std::vector<double> hpQ(videoParameters.activeVideoEnd + delay);


    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);

        // Feed zeros into the filter outside the active area
        for (qint32 h = videoParameters.activeVideoStart - delay; h < videoParameters.activeVideoStart; h++) {
            iFilter.feed(0.0);
            qFilter.feed(0.0);
        }
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            hpI[h] = iFilter.feed(I[h]);
            hpQ[h] = qFilter.feed(Q[h]);
        }
        for (qint32 h = videoParameters.activeVideoEnd; h < videoParameters.activeVideoEnd + delay; h++) {
            hpI[h] = iFilter.feed(0.0);
            hpQ[h] = qFilter.feed(0.0);
        }

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            // Offset to cover the filter delay
            double ai = hpI[h + delay];
            double aq = hpQ[h + delay];

            // Clip the filter strength
            if (fabs(ai) > nr_c) {
                ai = (ai > 0) ? nr_c : -nr_c;
            }
            if (fabs(aq) > nr_c) {
                aq = (aq > 0) ? nr_c : -nr_c;
            }

            I[h] -= ai;
            Q[h] -= aq;
        }
    }
}

void Comb::FrameBuffer::doYNR()
{
    if (configuration.yNRLevel == 0) return;

    // nr_y is the coring level
    double nr_y = configuration.yNRLevel * irescale;

    // High-pass filter for Y
    auto yFilter(f_nr);

    // Filter delay (since it's a symmetric FIR filter)
    const qint32 delay = c_nr_b.size() / 2;

    // High-pass result
    std::vector<double> hpY(videoParameters.activeVideoEnd + delay);

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *Y = componentFrame->y(lineNumber);

        // Feed zeros into the filter outside the active area
        for (qint32 h = videoParameters.activeVideoStart - delay; h < videoParameters.activeVideoStart; h++) {
            yFilter.feed(0.0);
        }
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            hpY[h] = yFilter.feed(Y[h]);
        }
        for (qint32 h = videoParameters.activeVideoEnd; h < videoParameters.activeVideoEnd + delay; h++) {
            hpY[h] = yFilter.feed(0.0);
        }

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            // Offset to cover the filter delay
            double a = hpY[h + delay];

            // Clip the filter strength
            if (fabs(a) > nr_y) {
                a = (a > 0) ? nr_y : -nr_y;
            }

            Y[h] -= a;
        }
    }
}

// Transform I/Q into U/V, and apply chroma gain
void Comb::FrameBuffer::transformIQ(double chromaGain, double chromaPhase)
{
    // Compute components for the rotation vector
    const double theta = ((33 + chromaPhase) * M_PI) / 180;
    const double bp = sin(theta) * chromaGain;
    const double bq = cos(theta) * chromaGain;

    // Apply the vector to all the samples
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *I = componentFrame->u(lineNumber);
        double *Q = componentFrame->v(lineNumber);

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            double U = (-bp * I[h]) + (bq * Q[h]);
            double V = ( bq * I[h]) + (bp * Q[h]);

            I[h] = U;
            Q[h] = V;
        }
    }
}

// Overlay the 3D filter map onto the output
void Comb::FrameBuffer::overlayMap(const FrameBuffer &previousFrame, const FrameBuffer &nextFrame)
{
    tbcDebugStream() << "Comb::FrameBuffer::overlayMap(): Overlaying map onto output";

    // Create a canvas for colour conversion
    FrameCanvas canvas(*componentFrame, videoParameters);

    // Convert CANDIDATE_SHADES into Y'UV form
    FrameCanvas::Colour shades[NUM_CANDIDATES];
    for (qint32 i = 0; i < NUM_CANDIDATES; i++) {
        const quint32 shade = CANDIDATE_SHADES[i];
        shades[i] = canvas.rgb(
            ((shade >> 16) & 0xff) << 8,
            ((shade >> 8) & 0xff) << 8,
            (shade & 0xff) << 8
        );
    }

    // For each sample in the frame...
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        double *U = componentFrame->u(lineNumber);
        double *V = componentFrame->v(lineNumber);

        // Fill the output frame with the RGB values
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            // Select the best candidate
            qint32 bestIndex;
            double bestSample;
            getBestCandidate(lineNumber, h, previousFrame, nextFrame, bestIndex, bestSample);

            // Leave Y' the same, but replace UV with the appropriate shade
            U[h] = shades[bestIndex].u;
            V[h] = shades[bestIndex].v;
        }
    }
}
