using System;
using NUnit.Framework;
using VhsDecodeAutoAudioAlign.TbcJson;

namespace VhsDecodeAutoAudioAlign.Test.UnitTest.tbcJson
{
    [TestFixture]
    public class TbcJsonFixupTest
    {

        [Test]
        public void Fixup32BitOverflow()
        {
            var doc = new TbcJsonDocument()
            {
                videoParameters = new TbcJsonDocument.VideoParameters()
                {
                    numberOfSequentialFields = 42,
                    system = "fancy"
                },
                fields = new TbcJsonDocument.Field[] 
                {
                     new TbcJsonDocument.Field(){ fileLoc = 2146744320, isFirstField = true, seqNo = 1 },
                     new TbcJsonDocument.Field(){ fileLoc = -2147424256, isFirstField = false, seqNo = 2 },
                     new TbcJsonDocument.Field(){ fileLoc = -2146656256, isFirstField = false, seqNo = 3 },
                }
            };

            var cut = new TbcJsonFixup(new ConsoleLog());
            var fix = cut.Fixup32BitOverflow(doc);

            Assert.That(fix.GetHashCode(), Is.Not.EqualTo(doc));
            Assert.That(fix.videoParameters.GetHashCode(), Is.EqualTo(doc.videoParameters.GetHashCode()));
            Assert.That(fix.fields.Length, Is.EqualTo(3));

            Assert.That(fix.fields[0].fileLoc, Is.EqualTo(2146744320L));
            Assert.That(fix.fields[0].isFirstField, Is.EqualTo(true));
            Assert.That(fix.fields[0].seqNo, Is.EqualTo(1));

            Assert.That(fix.fields[1].fileLoc, Is.EqualTo(2147543040L), nameof(TbcJsonDocument.Field.fileLoc) + " was not fixed");
            Assert.That(fix.fields[1].isFirstField, Is.EqualTo(false));
            Assert.That(fix.fields[1].seqNo, Is.EqualTo(2));

            Assert.That(fix.fields[2].fileLoc, Is.EqualTo(2148311040L), nameof(TbcJsonDocument.Field.fileLoc) + " was not fixed");
            Assert.That(fix.fields[2].isFirstField, Is.EqualTo(false));
            Assert.That(fix.fields[2].seqNo, Is.EqualTo(3));
        }
    }
}
