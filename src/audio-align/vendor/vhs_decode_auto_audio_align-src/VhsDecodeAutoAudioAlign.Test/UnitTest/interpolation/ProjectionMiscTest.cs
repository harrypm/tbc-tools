using System;
using NUnit.Framework;
using VhsDecodeAutoAudioAlign.Interpolation;

namespace VhsDecodeAutoAudioAlign.Test.UnitTest.interpolation
{
    class ProjectionMiscTest
    {
        const ulong FieldLength = 1000;

        static SignalConfig SomeConfig => new SignalConfig() 
        {
            sampleRateHz = (uint)(FieldLength * 50L),
            fieldRate = new FractionalUnsignedNumber()
            {
                nominator = 50,
                denominator = 1, 
            }
        };

        [Test]
        public void ExtractGapLessSectionMissingField()
        {
            var fieldOffsets = new ulong[]
            {
                FieldLength * 0,
                FieldLength * 1,
                FieldLength * 2,
                // missing
                FieldLength * 4,
                FieldLength * 5,
            };

            var sections = ProjectionMisc.ExtractGapLessSection(fieldOffsets, SomeConfig);

            Assert.That(sections.Length, Is.EqualTo(2));

            Assert.That(sections[0].fieldOffsets.Length, Is.EqualTo(3));
            Assert.That(sections[0].fieldOffsets[0], Is.EqualTo(FieldLength * 0));
            Assert.That(sections[0].fieldOffsets[1], Is.EqualTo(FieldLength * 1));
            Assert.That(sections[0].fieldOffsets[2], Is.EqualTo(FieldLength * 2));

            Assert.That(sections[1].fieldOffsets.Length, Is.EqualTo(2));
            Assert.That(sections[1].fieldOffsets[0], Is.EqualTo(FieldLength * 4));
            Assert.That(sections[1].fieldOffsets[1], Is.EqualTo(FieldLength * 5));
        }

        [Test]
        public void ExtractGapLessSectionMissingFieldSingle()
        {
            var fieldOffsets = new ulong[]
            {
                FieldLength * 0,
                FieldLength * 1,
                // 2 missing
                FieldLength * 3,
                // 4 missing
                FieldLength * 5,
                // 6 missing
                FieldLength * 7,
                FieldLength * 8,
            };

            var sections = ProjectionMisc.ExtractGapLessSection(fieldOffsets, SomeConfig);

            Assert.That(sections.Length, Is.EqualTo(4));
            Assert.That(sections[0].fieldOffsets.Length, Is.EqualTo(2));
            Assert.That(sections[1].fieldOffsets.Length, Is.EqualTo(1));
            Assert.That(sections[2].fieldOffsets.Length, Is.EqualTo(1));
            Assert.That(sections[3].fieldOffsets.Length, Is.EqualTo(2));
        }

        [Test]
        public void ExtractGapLessSectionStrayStartField()
        {
            var fieldOffsets = new ulong[]
            {
                FieldLength * 0,
                FieldLength * 10,
                FieldLength * 11,
                FieldLength * 12,
                FieldLength * 13,
            };

            var sections = ProjectionMisc.ExtractGapLessSection(fieldOffsets, SomeConfig);

            Assert.That(sections.Length, Is.EqualTo(2));
        }

        [Test]
        public void ExtractGapLessSectionStartLongGap()
        {
            var fieldOffsets = new ulong[]
            {
                FieldLength * 10,
                FieldLength * 11,
                FieldLength * 12,
                FieldLength * 13,
                // lots of nothing, then a new recording starts
                FieldLength * 50,
                FieldLength * 51,
                FieldLength * 52,
                FieldLength * 53,
            };

            var sections = ProjectionMisc.ExtractGapLessSection(fieldOffsets, SomeConfig);

            Assert.That(sections.Length, Is.EqualTo(2));
            Assert.That(sections[0].fieldOffsets[0], Is.EqualTo(FieldLength * 10));
            Assert.That(sections[1].fieldOffsets[0], Is.EqualTo(FieldLength * 50));
        }

        [Test]
        public void ExtractGapLessSectionStartShortGap()
        {
            var fieldOffsets = new ulong[]
            {
                FieldLength * 10,
                FieldLength * 11,
                FieldLength * 12,
                FieldLength * 13,
                // a second recording starts right in the middle of a field
                FieldLength * 14 - (FieldLength/2),
                FieldLength * 15 - (FieldLength/2),
                FieldLength * 16 - (FieldLength/2),
                FieldLength * 17 - (FieldLength/2),
            };

            var sections = ProjectionMisc.ExtractGapLessSection(fieldOffsets, SomeConfig);

            Assert.That(sections.Length, Is.EqualTo(2));
            Assert.That(sections[0].fieldOffsets[0], Is.EqualTo(FieldLength * 10));
            Assert.That(sections[1].fieldOffsets[0], Is.EqualTo(FieldLength * 14 - (FieldLength / 2)));
        }

        const uint IdealSampleRate = 1000u;
        uint ActualSampleRate => IdealSampleRate + 10;
        const ulong ActualStart = 4000UL;

        FractionalUnsignedNumber SomeFieldRate => new FractionalUnsignedNumber(10, 1);
        SignalConfig IdealSignal => new SignalConfig() { sampleRateHz = IdealSampleRate, fieldRate = SomeFieldRate };
        SignalConfig ActualSignal => new SignalConfig() { sampleRateHz = ActualSampleRate, fieldRate = SomeFieldRate };

        [Test]
        public void CalcLinearMappingParameters()
        {
            var actualOffsets = new FieldOffsetCalc(ActualSignal);

            var sections = new GapLessSection[]
            {
                new GapLessSection() { fieldOffsets = new ulong[]
                {
                    ActualStart + actualOffsets.FieldOffset(0),
                    ActualStart + actualOffsets.FieldOffset(1),
                    ActualStart + actualOffsets.FieldOffset(2),
                } },
                new GapLessSection() { fieldOffsets = new ulong[]
                {
                    // field 3 is missing
                    ActualStart + actualOffsets.FieldOffset(4),
                    ActualStart + actualOffsets.FieldOffset(5),
                } },
            };

            var projected = ProjectionMisc.CalcLinearMappingParameters(sections, IdealSignal);

            foreach(var p in projected)
                Console.WriteLine(ProjectionMisc.Info(p));

            Assert.That(projected.Length, Is.EqualTo(2));

            var correctedSampleRate = projected[0].outputSpeed * IdealSampleRate;
            Assert.That(correctedSampleRate, Is.EqualTo(ActualSampleRate), "speed was not calculated correctly");

            Assert.That(projected[0].outputStart, Is.EqualTo(0));
            Assert.That(projected[1].outputStart, Is.EqualTo(3 * 100));

            Assert.That(projected[0].inputStart, Is.EqualTo(ActualStart));
            Assert.That(projected[1].inputStart, Is.EqualTo(ActualStart + actualOffsets.FieldOffset(4)));
        }

        [Test]
        public void CalcLinearMappingParametersSingleField()
        {
            var actualOffsets = new FieldOffsetCalc(ActualSignal);

            var sections = new GapLessSection[]
            {
                new GapLessSection() { fieldOffsets = new ulong[]
                {
                    ActualStart + actualOffsets.FieldOffset(0),
                    ActualStart + actualOffsets.FieldOffset(1),
                } },
                new GapLessSection() { fieldOffsets = new ulong[]
                {
                    // field 2 is missing
                    ActualStart + actualOffsets.FieldOffset(3),
                } },
            };

            var projected = ProjectionMisc.CalcLinearMappingParameters(sections, IdealSignal);

            foreach (var p in projected)
                Console.WriteLine(ProjectionMisc.Info(p));

            Assert.That(projected.Length, Is.EqualTo(2));

            Assert.That(projected[1].outputSpeed.ToString(), Is.EqualTo((new FractionalUnsignedNumber(1,1)) .ToString()), "speed was not set to 1.0");
            Assert.That(projected[1].outputStart, Is.EqualTo(2 * 100));
            Assert.That(projected[1].inputStart, Is.EqualTo(ActualStart + actualOffsets.FieldOffset(3)));
        }

        [Test]
        public void CalcLinearMappingParametersSingleFieldStart()
        {
            var actualOffsets = new FieldOffsetCalc(ActualSignal);

            var sections = new GapLessSection[]
            {
                new GapLessSection() { fieldOffsets = new ulong[]
                {
                    ActualStart + actualOffsets.FieldOffset(0),
                } },
            };

            var projected = ProjectionMisc.CalcLinearMappingParameters(sections, IdealSignal);

            foreach (var p in projected)
                Console.WriteLine(ProjectionMisc.Info(p));

            Assert.That(projected.Length, Is.EqualTo(1));

            Assert.That(projected[0].outputSpeed.nominator, Is.EqualTo(1), "speed was not extrapolated correctly");
            Assert.That(projected[0].outputSpeed.denominator, Is.EqualTo(1), "speed was not extrapolated correctly");
            Assert.That(projected[0].outputStart, Is.EqualTo(0));
            Assert.That(projected[0].inputStart, Is.EqualTo(ActualStart));
        }

        [Test]
        [TestCase(2)]
        [TestCase(3)]
        [TestCase(5)]
        [TestCase(7)]
        [TestCase(11)]
        [TestCase(13)]
        [TestCase(3041)]
        public void LimitPrecision21BitNoLimiting(int prime)
        {
            var n = new FractionalUnsignedNumber(3,7);
            while (n.nominator < 1024 * 1024 * 2)
            {
                n.nominator *= (uint)prime;
                n.denominator *= (uint)prime;
            }

            ProjectionMisc.LimitPrecision21Bit(n);

            Assert.That(n.nominator, Is.EqualTo(3));
            Assert.That(n.denominator, Is.EqualTo(7));
        }

        [Test]
        public void LimitPrecision21BitPrimeWithLimiting()
        {
            // both numbers are prime, greater than 21 bit
            var n = new FractionalUnsignedNumber(3647339, 3146839);
            // => 1.15904849279

            ProjectionMisc.LimitPrecision21Bit(n);

            // => 1.15904854333
            Assert.That(n.nominator, Is.EqualTo(1823669));
            Assert.That(n.denominator, Is.EqualTo(1573419));
        }
    }
}
