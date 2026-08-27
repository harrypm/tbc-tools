using System;
using System.Linq;
using NUnit.Framework;
using VhsDecodeAutoAudioAlign.Interpolation;

namespace VhsDecodeAutoAudioAlign.Test.UnitTest.interpolation
{
    [TestFixture]
    public class LinearStreamProjectionTest
    {
        ConsoleLog log;

        [SetUp]
        public void Init()
        {
            log = new ConsoleLog();
        }

        [TearDown]
        public void Cleanup()
        {
        }

        [Test]
        public void ProjectOffset()
        {
            var rfSampleRate = (uint)(40 * 1000 * 1000);
            var ideal = new SignalConfig() { fieldRate = Consts.FieldRatePal, sampleRateHz = rfSampleRate };
            var actual = new SignalConfig() { fieldRate = new FractionalUnsignedNumber(50000000, 1000112), sampleRateHz = rfSampleRate };
            var field0 = 1234567ul;
            var actualOffsets = new Interpolation.FieldOffsetCalc(actual);
            var projectedSampleRate = 48000u;
            var totalFields = 85000;

            var fieldOffsets = Enumerable
                .Range(0, totalFields)
                .Select(i => field0 + actualOffsets.FieldOffset((uint)i))
                .ToArray();


            var cut = new LinearStreamProjection(log, ideal, fieldOffsets, projectedSampleRate);

            var start = cut.ProjectOutputToInputOffset(0);
            Assert.That(start, Is.EqualTo(new FractionalUnsignedNumber(projectedSampleRate, rfSampleRate) * field0));

            var end = cut.ProjectOutputToInputOffset((ulong)(projectedSampleRate * totalFields / 50));

            var compensatedOutputSampleRate = new FractionalUnsignedNumber(end - start, (ulong)(totalFields / 50));
            var expectedOutputSampleRate = new FractionalUnsignedNumber(actual.fieldRate.denominator * 50 * projectedSampleRate, actual.fieldRate.nominator);

            // 5 ms (in samples) across 2h (60 * 60 * 2) 
            var tolerance = ((double)(5 * 48)) / (60.0 * 60.0 * 2.0);
            Assert.That(compensatedOutputSampleRate.ToDouble(), Is.InRange(expectedOutputSampleRate.ToDouble() - tolerance, expectedOutputSampleRate.ToDouble() + tolerance));
        }
    }
}
