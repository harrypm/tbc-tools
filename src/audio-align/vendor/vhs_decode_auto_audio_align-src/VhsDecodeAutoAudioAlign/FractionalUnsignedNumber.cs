using System;
namespace VhsDecodeAutoAudioAlign
{
    static class FractionalUnsignedNumberExtensions
    {
        public static ulong Multiply(this ulong me, FractionalUnsignedNumber fractional)
        {
            return fractional * me;
        }

        public static ulong Divide(this ulong me, FractionalUnsignedNumber fractional)
        {
            var res = me * fractional.denominator;
            return res / fractional.nominator;
        }
    }


    class FractionalUnsignedNumber
    {
        public ulong nominator;
        public ulong denominator;

        public FractionalUnsignedNumber()
            : this(0, 1)
        {
        }

        public FractionalUnsignedNumber(ulong nominator, ulong denominator)
        {
            if (denominator == 0)
                throw new ArgumentException(nameof(denominator) + " must be greater than 0");

            this.nominator = nominator;
            this.denominator = denominator;
        }

        public static ulong operator *(FractionalUnsignedNumber a, ulong b)
        {
            var res = b * a.nominator;
            return res / a.denominator;
        }

        public override string ToString()
        {
            return nominator + "/" + denominator;
        }

        public double ToDouble()
        {
            double factor = nominator;
            factor /= denominator;
            return factor;
        }
    }
}
