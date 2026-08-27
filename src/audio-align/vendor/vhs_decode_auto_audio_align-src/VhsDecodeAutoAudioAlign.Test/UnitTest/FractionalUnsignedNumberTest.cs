using System;
using NUnit.Framework;

namespace VhsDecodeAutoAudioAlign.Test.UnitTest
{

    public class FractionalUnsignedNumberTest
    {
        [Test]
        public void Ctor()
        {
            var f = new FractionalUnsignedNumber(1, 2);
            Assert.That(f.nominator, Is.EqualTo(1));
            Assert.That(f.denominator, Is.EqualTo(2));
        }

        [Test]
        public void DeNominator0()
        {
            Assert.Throws<ArgumentException>(() => new FractionalUnsignedNumber(1, 0));
        }

        static TestCaseData[] MultiplyCases => new TestCaseData[]
        {
            new TestCaseData(1UL, 2UL, 10UL, 5UL),
            new TestCaseData(1UL, 3UL, 33UL, 11UL),
        };

        [Test]
        [TestCaseSource(nameof(MultiplyCases))]
        public void MultiplyOperator(ulong nom, ulong denom, ulong factor, ulong result)
        {
            var f = new FractionalUnsignedNumber(nom, denom);

            var n = f * factor;

            Assert.That(n, Is.EqualTo(result));
        }

        [Test]
        [TestCaseSource(nameof(MultiplyCases))]
        public void MultiplyExtension(ulong nom, ulong denom, ulong factor, ulong result)
        {
            var f = new FractionalUnsignedNumber(nom, denom);

            var n = factor.Multiply(f);

            Assert.That(n, Is.EqualTo(result));
        }

        static TestCaseData[] DivCases => new TestCaseData[]
        {
            new TestCaseData(1UL, 2UL, 10UL, 20UL),
        };

        [Test]
        [TestCaseSource(nameof(DivCases))]
        public void DivExtension(ulong nom, ulong denom, ulong intNom, ulong result)
        {
            var f = new FractionalUnsignedNumber(nom, denom);

            var n = intNom.Divide(f);

            Assert.That(n, Is.EqualTo(result));
        }
    }
}
