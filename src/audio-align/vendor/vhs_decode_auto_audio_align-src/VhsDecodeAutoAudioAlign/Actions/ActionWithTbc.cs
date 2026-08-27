using System;
using System.IO;
using System.Linq;
using Binah;
using VhsDecodeAutoAudioAlign.TbcJson;

namespace VhsDecodeAutoAudioAlign.Actions
{
    abstract class ActionWithTbcArgs
    {
        [CommandLine("--json", "The json from a TBC file", false)]
        public FileInfo json;

        [CommandLine("--rf-video-sample-rate-hz", "The sample rate from the original RF video capture")]
        public uint videoRfSampleRateHz = 40000000;

    }

    abstract class ActionWithTbc<ARGS> : IAction<ARGS>
        where ARGS : ActionWithTbcArgs, new()
    {
        protected readonly ILog log;

        protected ActionWithTbc(ILog log)
        {
            this.log = log ?? throw new ArgumentNullException(nameof(log));
        }

        public abstract string Switch { get; }
        public abstract string Info { get; }

        public abstract void Run(ARGS args, string[] unconsumend);

        protected SignalConfig ConfigFromTbc(ActionWithTbcArgs args, TbcJsonDocument json)
        {
            if (json.videoParameters == null)
                throw new ArgumentException("The TBC json did not contain " + nameof(json.videoParameters) + " can't get video system");
            if( string.IsNullOrWhiteSpace( json.videoParameters.system) )
                throw new ArgumentException("The TBC json " + nameof(json.videoParameters) + " did not contain the video system");


            var system = json.videoParameters.system.ToLower().Trim(' ', '\t', '\n', '\r');

            var cfg = new SignalConfig() 
            { 
                sampleRateHz = args.videoRfSampleRateHz 
            };

            if(system.StartsWith("pal-m"))
                cfg.fieldRate = Consts.FieldRateNtsc;
            else if(system.Contains("pal") && system.Contains("60") == false)
                cfg.fieldRate = Consts.FieldRatePal;
            else if (system.Contains("ntsc") && system.Contains("50") == false)
                cfg.fieldRate = Consts.FieldRateNtsc;
            else
                throw new NotSupportedException("I don't support system '" + system + "'");
                
            return cfg;
        }

        protected TbcJsonDocument ReadTbcJson(ActionWithTbcArgs args)
        {
            var fixup = new TbcJsonFixup(log);
            var reader = new TbcJsonReader(log);

            log.That("Will read '" + args.json.FullName + "' now ...");
            var start = DateTime.Now;
            var tbc = reader.ReadTbcJson(args.json);
            tbc = fixup.Fixup32BitOverflow(tbc);
            var time = DateTime.Now - start;
            log.That("... done, took " + time.TotalMilliseconds + " ms, and loaded " + tbc.fields.Length + " fields");
            return tbc;
        }

        protected ulong[] GetFieldLocations(TbcJsonDocument json)
        {
            return json.fields.Select(f => (ulong)f.fileLoc).ToArray();
        }
    }
}
