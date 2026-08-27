using System;
using System.Collections.Generic;
using System.Linq;

namespace VhsDecodeAutoAudioAlign.Interpolation
{
    class LinearStreamProjection
    {
        readonly ILog log;
        readonly SignalConfig referenceVideoStream;
        readonly ulong[] referenceVideoFieldPositions;
        readonly uint projectedSampleRateHz;
        readonly GapLessSectionLinearProjectionScaled[] gapLessesSectionsProjectionScaled;

        readonly FractionalUnsignedNumber sourceToProjectedSampleRate;

        public LinearStreamProjection(ILog log, SignalConfig referenceVideoStream, ulong[] referenceVideoFieldPositions, uint projectedSampleRateHz)
        {
            this.log = log ?? throw new ArgumentNullException(nameof(log));
            this.referenceVideoStream = referenceVideoStream ?? throw new ArgumentNullException(nameof(referenceVideoStream));
            this.referenceVideoFieldPositions = referenceVideoFieldPositions ?? throw new ArgumentNullException(nameof(referenceVideoFieldPositions));
            this.projectedSampleRateHz = projectedSampleRateHz;

            sourceToProjectedSampleRate = new FractionalUnsignedNumber(projectedSampleRateHz, referenceVideoStream.sampleRateHz);
            ProjectionMisc.LimitPrecision21Bit(sourceToProjectedSampleRate);
            log.That("Reference to output sample rate scaling: " + sourceToProjectedSampleRate);

            var gapLessesSections = ProjectionMisc.ExtractGapLessSection(referenceVideoFieldPositions, referenceVideoStream);
            var gapLessesSectionsProjection = ProjectionMisc.CalcLinearMappingParameters(gapLessesSections, referenceVideoStream);
            ProjectionMisc.LimitPrecision21Bit(gapLessesSectionsProjection);
            gapLessesSectionsProjectionScaled = ToScaled(gapLessesSectionsProjection, sourceToProjectedSampleRate);

            foreach (var s in gapLessesSectionsProjectionScaled)
                log.That("Section @" + s.fieldOffsets[0] + " -> " + s.outputStart + " with " + s.fieldOffsets.Length + " fields, " + s.outputSpeed + " factor=" + s.outputSpeed.ToDouble());
        }

        class GapLessSectionLinearProjectionScaled : GapLessSectionLinearProjection
        {
            public ulong scaledInputStart;
            public ulong scaledOutputStart;
        }

        GapLessSectionLinearProjectionScaled[] ToScaled(GapLessSectionLinearProjection[] unscaled, FractionalUnsignedNumber sampleRateScaling)
        {
            return unscaled
                .Select(u => new GapLessSectionLinearProjectionScaled() 
                {
                    fieldOffsets = u.fieldOffsets,
                    inputStart = u.inputStart,
                    outputStart = u.outputStart,
                    outputSpeed = u.outputSpeed,
                    scaledInputStart = sampleRateScaling * u.inputStart,
                    scaledOutputStart = sampleRateScaling * u.outputStart,
                })
                .ToArray();
        }

        GapLessSectionLinearProjectionScaled GetSection(ulong outputOffset)
        {
            var last = gapLessesSectionsProjectionScaled[0];

            foreach (var s in gapLessesSectionsProjectionScaled)
            {
                if (s.scaledOutputStart > outputOffset)
                    return last;
                last = s;
            }

            return last;
        }

        public ulong ProjectOutputToInputOffset(ulong outputOffset)
        {
            // TODO super inefficient!!
            var section = GetSection(outputOffset);


            var inputOffset = outputOffset - section.scaledOutputStart;
            inputOffset = section.outputSpeed * inputOffset;
            inputOffset += section.scaledInputStart;
            return inputOffset;
        }


    }
}
