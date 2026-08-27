using System;
using NUnit.Framework;
using VhsDecodeAutoAudioAlign.Interpolation;

namespace VhsDecodeAutoAudioAlign.Test.UnitTest.interpolation
{
    [TestFixture]
    public class FieldOffsetCalcTest
    {
        [Test]
        [TestCase(0u, 0L)]
        [TestCase(1u, 800000L)]
        [TestCase(50u, 40000000L)]
        public void Pal40Msps(uint field, long off)
        {
            var cut = new FieldOffsetCalc(new SignalConfig()
            {
                sampleRateHz = 40 * 1000 * 1000,
                fieldRate = Consts.FieldRatePal
            });

            Assert.That(cut.FieldOffset(field), Is.EqualTo(off));
        }

        [Test]
        [TestCase(0u, 0L)]
        [TestCase(1u, 667333L)]
        [TestCase(60u, 40040000L)]
        public void Ntsc40Msps(uint field, long off)
        {
            var cut = new FieldOffsetCalc(new SignalConfig()
            {
                sampleRateHz = 40 * 1000 * 1000,
                fieldRate = Consts.FieldRateNtsc
            });

            Assert.That(cut.FieldOffset(field), Is.EqualTo(off));
        }
    }
}
