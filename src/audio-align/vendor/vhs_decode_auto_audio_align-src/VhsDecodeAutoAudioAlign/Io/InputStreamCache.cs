using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace VhsDecodeAutoAudioAlign.Io
{
    class InputStreamCacheConfig
    {
        public int sampleSizeInBytes;
        public int blockSizeInSamples;
        public int blocksToCache;
    }

    class InputStreamCache
    {
        readonly ILog log;
        readonly Stream input;
        readonly InputStreamCacheConfig config;

        public InputStreamCache(ILog log, Stream input, InputStreamCacheConfig config)
        {
            this.log = log ?? throw new ArgumentNullException(nameof(log));
            this.input = input ?? throw new ArgumentNullException(nameof(input));
            this.config = config ?? throw new ArgumentNullException(nameof(config));

            if (config.sampleSizeInBytes < 1)
                throw new ArgumentException(nameof(config) + "." + nameof(config.sampleSizeInBytes) + " must be greater than 0");
            if (config.blockSizeInSamples < 1)
                throw new ArgumentException(nameof(config) + "." + nameof(config.blockSizeInSamples) + " must be greater than 0");
            if (config.blocksToCache < 1)
                throw new ArgumentException(nameof(config) + "." + nameof(config.blocksToCache) + " must be greater than 0");

            var max = config.blockSizeInSamples * config.blocksToCache * config.sampleSizeInBytes;
            log.That("Input cache setup with a maximum of " + max + " bytes (" + (max / (1024 * 1024)) + " Mb)");
        }

        class CachedBlock
        {
            public ulong blockIdx;
            public byte[] data;
        }

        bool eof = false;
        readonly List<CachedBlock> cache = new List<CachedBlock>();

        byte[] ReadNextBlock()
        {
            if (eof)
                return null;

            try
            {
                var block = new byte[config.blockSizeInSamples * config.sampleSizeInBytes];

                var off = 0;
                while (off < block.Length)
                {
                    var read = input.Read(block, off, block.Length - off);
                    if (read == 0) // EOF
                    {
                        eof = true;
                        var lastBlock = block.Take(off).ToArray();
                        log.That("End of input stream (" + input.PositionStringSafe() + "/" + lastBlock.Length + ")");
                        return lastBlock;
                    }

                    if (read > 0)
                        off += read;
                }

                return block;
            }
            catch (Exception x)
            {
                log.That(x);
            }

            return null;
        }

        bool CacheNextBlock()
        {
            var next = ReadNextBlock();
            if (next == null)
                return false;

            var nextBlockIdx = cache.Count == 0 ? 0 : cache.Last().blockIdx + 1;
            cache.Add(new CachedBlock() { blockIdx = nextBlockIdx, data = next });

            while (cache.Count > config.blocksToCache)
                cache.RemoveAt(0);

            return true;
        }

        CachedBlock TryGetBlock(ulong blockIdx)
        {
            if (cache.Count == 0)
                return null;

            var firstCachedBlockIdx = cache[0].blockIdx;
            if (firstCachedBlockIdx > blockIdx)
            {
                log.That("Requested block " + blockIdx + " which was before first cached block " + firstCachedBlockIdx + ". Can't fulfill the request as cache is too small. Make chache bigger, will bail now!");
                throw new IOException("Cache too small, can't fulfill request");
            }

            var cacheIdx = (int)(blockIdx - firstCachedBlockIdx);
            if (cacheIdx >= cache.Count)
                return null; // block not yet in cache

            return cache[cacheIdx];
        }

        public byte[] GetSample(ulong sampleOffset)
        {
            var blockIdx = sampleOffset / (ulong)config.blockSizeInSamples;
            var blockOff = sampleOffset % (ulong)config.blockSizeInSamples;

            while (true)
            {
                var cacheBlock = TryGetBlock(blockIdx);
                if (cacheBlock != null)
                {
                    var sampleData = new byte[config.sampleSizeInBytes];

                    var sourceOff = (int)blockOff * config.sampleSizeInBytes;
                    var length = config.sampleSizeInBytes;

                    if ((sourceOff + length) > cacheBlock.data.Length)
                    {
                        log.That("Read beyond end of last cached block (" + blockIdx + "+" + blockOff + "), can't get sample " + sampleOffset);
                        return null;
                    }

                    Array.Copy(cacheBlock.data, sourceOff, sampleData, 0, length);

                    return sampleData;
                }

                if (CacheNextBlock() == false)
                    return null; // EOF
            }
        }
    }
}
