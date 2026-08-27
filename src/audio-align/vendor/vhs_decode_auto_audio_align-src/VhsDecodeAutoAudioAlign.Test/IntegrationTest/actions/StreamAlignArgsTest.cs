using System;
using System.IO;
using NUnit.Framework;
using VhsDecodeAutoAudioAlign.Actions;

namespace VhsDecodeAutoAudioAlign.Test.IntegrationTest.actions
{
    [TestFixture]
    public class StreamAlignArgsTest
    {
        ConsoleLog log;
        TempDir tmp;

        [SetUp]
        public void Init()
        {
            log = new ConsoleLog();
            tmp = new TempDir();
        }

        [TearDown]
        public void Cleanup()
        {
            tmp.Dispose();
        }

        string[] SimpleJson => new string[] 
        {
            "{",
            "\"videoParameters\":{ \"numberOfSequentialFields\":3, \"system\":\"PAL\" },",
            "\"fields\":[{\t\"isFirstField\":true, \"seqNo\":1, \"fileLoc\":0 }]",
            "}",
        };

        [Test]
        public void RunWithFiles()
        {
            var inputData = new byte[42];
            var inputFile = tmp.dir.File("input");
            File.WriteAllBytes(inputFile.FullName, inputData);
            var outputFile = tmp.dir.File("output");

            var jsonFile = tmp.dir.File("some.tbc.json");
            File.WriteAllLines(jsonFile.FullName, SimpleJson);

            var cut = new StreamAlign(log);

            cut.Run(new StreamAlignArgs() 
            {
                allowOverwritingOutput = false,
                inputSampleRateHz = 1000,
                inputSampleSizeBytes = 1,
                inputFile = inputFile,
                outputFile = outputFile,
                json = jsonFile,
                videoRfSampleRateHz = 1000,
            }, new string[0]);


            outputFile.Refresh();
            Assert.That(outputFile.Length, Is.EqualTo(inputData.Length));
        }
    }
}
