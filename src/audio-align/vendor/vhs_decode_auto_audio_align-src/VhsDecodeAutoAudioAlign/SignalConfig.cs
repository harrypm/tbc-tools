using System;
namespace VhsDecodeAutoAudioAlign
{
    class SignalConfig
    {
        /// <summary>
        /// Ususally something like 40000000 (40MSps)
        /// </summary>
        public uint sampleRateHz;

        /// <summary>
        /// The field rate.
        /// </summary>
        public FractionalUnsignedNumber fieldRate;
    }
}
