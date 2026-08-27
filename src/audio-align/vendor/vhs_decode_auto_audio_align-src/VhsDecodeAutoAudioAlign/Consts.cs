using System;
namespace VhsDecodeAutoAudioAlign
{
    static class Consts
    {
        public static FractionalUnsignedNumber FieldRatePal => new FractionalUnsignedNumber(50, 1);
        public static FractionalUnsignedNumber FieldRateNtsc => new FractionalUnsignedNumber(60000, 1001);
    }
}
