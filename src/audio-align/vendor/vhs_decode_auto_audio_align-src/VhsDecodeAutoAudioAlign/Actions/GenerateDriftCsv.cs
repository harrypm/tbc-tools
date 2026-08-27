using System;
using System.IO;
using System.Linq;
using Binah;
using VhsDecodeAutoAudioAlign.TbcJson;

namespace VhsDecodeAutoAudioAlign.Actions
{
    class GenerateDriftCsvArgs : ActionWithTbcArgs
    {
        [CommandLine("--csv-output", "The output csv", false)]
        public FileInfo csv;

        [CommandLine("--csv-seperator", "The csv seperator")]
        public string csvSep = ",";

        [CommandLine("--csv-new-line", "The csv new line")]
        public string csvNl = "\\n";
    }

    class GenerateDriftCsv : ActionWithTbc<GenerateDriftCsvArgs>
    {
        public GenerateDriftCsv(ILog log) : base(log)
        {
        }

        public override string Switch => "gen-drift-csv";

        public override string Info => "Generates a CSV that shows the drift of the video fields in the video RF stream";

        static string Csv(string txt)
        {
            return "\"" + txt + "\"";
        }

        static string Csv(ulong n)
        {
            return "" + n;
        }

        static string Csv(long n)
        {
            return "" + n;
        }

        static string Csv(double n, uint digits = 2)
        {
            return "" + n.ToString("F" + digits);
        }

        public override void Run(GenerateDriftCsvArgs args, string[] unconsumend)
        {
            var json = ReadTbcJson(args);
            var cfg = ConfigFromTbc(args, json);
            var sep = GetSep(args);

            var offsetCalc = new Interpolation.FieldOffsetCalc(cfg);
            var projection = new Interpolation.LinearStreamProjection(log, cfg, GetFieldLocations(json), cfg.sampleRateHz);

            using (var csv = new StreamWriter(args.csv.FullName))
            {
                GetNl(args, csv);

                csv.WriteLine(
                    Csv("video time [s]") + sep +
                    Csv("capture time [s]") + sep +
                    Csv("field") + sep +
                    Csv("expected offset") + sep +
                    Csv("actual offset") + sep +
                    Csv("projected offset") + sep +
                    Csv("error actual [samples]") + sep +
                    Csv("projection error [samples]") + sep +
                    Csv("projection error [ms]"));

                var field0 = (ulong)json.fields[0].fileLoc;
                for (int i = 0; i < json.fields.Length; ++i)
                {
                    var f = json.fields[i];

                    var outputOffset = offsetCalc.FieldOffset((uint)i);
                    var expectedOff = field0 + outputOffset;
                    var actualOff = f.fileLoc;
                    var projectedOff = projection.ProjectOutputToInputOffset(outputOffset);

                    var videoTimeSec = (double)outputOffset / (double)cfg.sampleRateHz;
                    var captureTimeSec = (double)actualOff / (double)cfg.sampleRateHz;

                    var errorActual = (long)actualOff - (long)expectedOff;
                    var projectionErr = (long)projectedOff - (long)actualOff;

                    double projectionErrMs = projectionErr;
                    projectionErrMs /= (cfg.sampleRateHz / 1000);

                    csv.WriteLine(
                        Csv(videoTimeSec, 5) + sep +
                        Csv(captureTimeSec, 5) + sep +
                        Csv(i) + sep +
                        Csv(expectedOff) + sep +
                        Csv(actualOff) + sep +
                        Csv(projectedOff) + sep +
                        Csv(errorActual) + sep +
                        Csv(projectionErr) + sep +
                        Csv(projectionErrMs));
                }
            }

            log.That("Done writing CSV to '" + args.csv.FullName + "'");
        }



        static void GetNl(GenerateDriftCsvArgs args, StreamWriter csv)
        {
            csv.NewLine = string.IsNullOrWhiteSpace(args.csvNl) ? "\n" : args.csvNl.Replace("\\n", "\n").Replace("\\r", "\r");
        }

        static string GetSep(GenerateDriftCsvArgs args)
        {
            return string.IsNullOrWhiteSpace(args.csvSep) ? "," : args.csvSep;
        }
    }
}
