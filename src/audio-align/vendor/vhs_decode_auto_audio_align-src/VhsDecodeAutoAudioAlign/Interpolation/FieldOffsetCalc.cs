using System;
namespace VhsDecodeAutoAudioAlign.Interpolation
{
    class FieldOffsetCalc
    {
        readonly SignalConfig cfg;

        public FieldOffsetCalc(SignalConfig cfg)
        {
            this.cfg = cfg ?? throw new ArgumentNullException(nameof(cfg));
            if (cfg.fieldRate == null)
                throw new ArgumentException(nameof(cfg.fieldRate) + " is null");
            if (cfg.sampleRateHz < 1)
                throw new ArgumentException(nameof(cfg.sampleRateHz) + " must be positive");
        }

        public ulong FieldOffset(uint field)
        {
            ulong off = field;
            off *= cfg.sampleRateHz;
            return off.Divide(cfg.fieldRate);
        }
    }
}
