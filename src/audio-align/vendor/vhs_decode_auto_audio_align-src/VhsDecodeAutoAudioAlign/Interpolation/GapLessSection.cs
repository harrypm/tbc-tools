using System;
namespace VhsDecodeAutoAudioAlign.Interpolation
{
    class GapLessSection
    {
        public ulong[] fieldOffsets;
    }

    class GapLessSectionProjection : GapLessSection
    {
        public ulong inputStart;
        public ulong outputStart;
    }

    class GapLessSectionLinearProjection : GapLessSectionProjection
    {
        public FractionalUnsignedNumber outputSpeed;
    }
}
