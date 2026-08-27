using System;
using System.IO;
using Binah;

namespace VhsDecodeAutoAudioAlign.Actions
{
    class StreamAlignArgs : ActionWithTbcArgs
    {
        [CommandLine("--cache-block-size-samples", "The cache block size in samples")]
        public uint cacheBlockSizeInSamples = 1024*1024; // divisible by 4096, which is a popular page size in modern OS

        [CommandLine("--cache-size", "The number of blocks to keep in cache")]
        public uint cacheBlocksToCache = 16; // resulting in a couple of MB

        [CommandLine("--sample-size-bytes", "Size of a single input sample in bytes, e.g. a 16 Bit stereo input has 4-byte sample", false)]
        public uint inputSampleSizeBytes;

        [CommandLine("--stream-sample-rate-hz", "The sample rate of the stream to align in Hz, e.g. 48000", false)]
        public uint inputSampleRateHz;

        [CommandLine("--input-file", "The input file, uses stdin if not set")]
        public FileInfo inputFile = null;

        [CommandLine("--output-file", "The output file, uses stdout if not set")]
        public FileInfo outputFile = null;

        [CommandLine("--overwrite", "Will overwrite an existing output file")]
        public object allowOverwritingOutput = null;

        public enum DropFieldBehavior
        {
            Skip
        }

        [CommandLine("--drop-field-behavior", "Configures how missing video fields should be treated on the input stream.")]
        public DropFieldBehavior dropFieldBehavior = DropFieldBehavior.Skip;
    }

    class StreamAlign : ActionWithTbc<StreamAlignArgs>
    {
        public StreamAlign(ILog log) : base(log)
        {
        }

        public override string Switch => "stream-align";

        public override string Info => "Aligns an input stream to a reference RF video stream";

        Stream GetInputStream(StreamAlignArgs args)
        {
            if (args.inputFile == null)
                return Console.OpenStandardInput();

            args.inputFile.Refresh();
            if (args.inputFile.Exists == false)
                throw new FileNotFoundException(args.inputFile.FullName);

            return args.inputFile.Open(FileMode.Open, FileAccess.Read, FileShare.Read);
        }

        Stream GetOutputStream(StreamAlignArgs args)
        {
            if (args.outputFile == null)
                return Console.OpenStandardOutput();

            args.outputFile.Refresh();
            if (args.outputFile.Exists)
            {
                if (args.allowOverwritingOutput == null)
                    throw new ArgumentException("output file '" + args.outputFile.FullName + "' exists, will not overwrite");

                log.That("Will overwrite '" + args.outputFile.FullName + "'");
            }

            return args.outputFile.Open(FileMode.Create, FileAccess.Write, FileShare.Read);
        }

        Io.InputStreamCacheConfig GetCacheConfig(StreamAlignArgs args)
        {
            return new Io.InputStreamCacheConfig() 
            {
                blocksToCache = (int)args.cacheBlocksToCache,
                blockSizeInSamples = (int)args.cacheBlockSizeInSamples,
                sampleSizeInBytes = (int)args.inputSampleSizeBytes,
            };
        }

        DateTime lastReport = DateTime.MinValue;
        ulong lastSamples = 0;

        void ReportSpeed(ulong outputSamples, uint inputSampleRateHz)
        {
            if ((outputSamples & 0xff) != 0)
                return; // we'll just check every 256 samples to not wast too much time doing statistics ...

            var now = DateTime.Now;
            var time = now - lastReport;

            if (time < TimeSpan.FromSeconds(1))
                return;

            var samples = outputSamples - lastSamples;

            lastSamples = outputSamples;
            lastReport = now;

            var sps = (double)samples / time.TotalSeconds;

            var location = TimeSpan.FromTicks((TimeSpan.TicksPerSecond * (long)outputSamples) / (long)inputSampleRateHz);

            log.That("Processing " + sps.ToString("F1") + " samples/s, at output location " + location);
        }

        public override void Run(StreamAlignArgs args, string[] unconsumend)
        {
            var cacheCfg = GetCacheConfig(args);
            var videoJson = ReadTbcJson(args);
            var videoConfig = ConfigFromTbc(args, videoJson);

            var projection = new Interpolation.LinearStreamProjection(log, videoConfig, GetFieldLocations(videoJson), args.inputSampleRateHz);

            using (var input = GetInputStream(args))
            using (var output = GetOutputStream(args))
            {
                var cache = new Io.InputStreamCache(log, input, cacheCfg);

                ulong outputOffset = 0;
                byte[] sample;
                do
                {
                    var inputOffset = projection.ProjectOutputToInputOffset(outputOffset);
                    sample = cache.GetSample(inputOffset);

                    if (sample != null)
                    {
                        output.Write(sample, 0, sample.Length);
                        ++outputOffset;
                        ReportSpeed(outputOffset, args.inputSampleRateHz);
                    }
                }
                while (sample != null);


                log.That("End of stream processing, " + output.PositionStringSafe() + " total output bytes (" + outputOffset + "samples), total " + input.PositionStringSafe() + " input bytes");
            }
        }
    }
}
