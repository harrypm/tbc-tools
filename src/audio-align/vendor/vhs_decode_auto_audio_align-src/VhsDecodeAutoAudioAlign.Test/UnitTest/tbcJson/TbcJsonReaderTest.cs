using System;
using NUnit.Framework;
using VhsDecodeAutoAudioAlign.TbcJson;
using VhsDecodeAutoAudioAlign.Test.TestData;

namespace VhsDecodeAutoAudioAlign.Test.UnitTest.tbcJson
{
    [TestFixture]
    public class TbcJsonReaderTest
    {
        [Test]
        public void ReadDemo()
        {
            var demo = TestDataManager.DemoTbcJson;

            var cut = new TbcJsonReader(new ConsoleLog());
            var tbc = cut.ReadTbcJson(demo);

            Assert.That(tbc.fields.Length, Is.EqualTo(3));

            Assert.That(tbc.fields[0].seqNo, Is.EqualTo(1));
            Assert.That(tbc.fields[1].seqNo, Is.EqualTo(2));
            Assert.That(tbc.fields[2].seqNo, Is.EqualTo(3));

            Assert.That(tbc.fields[0].isFirstField, Is.True);
            Assert.That(tbc.fields[1].isFirstField, Is.False);
            Assert.That(tbc.fields[2].isFirstField, Is.True);

            Assert.That(tbc.fields[0].fileLoc, Is.EqualTo(31979520));
            Assert.That(tbc.fields[1].fileLoc, Is.EqualTo(32778240));
            Assert.That(tbc.fields[2].fileLoc, Is.EqualTo(33576960));
        }
    }
}
