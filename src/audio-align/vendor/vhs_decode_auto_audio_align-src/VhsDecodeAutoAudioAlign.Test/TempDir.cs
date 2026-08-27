using System;
using System.IO;

namespace VhsDecodeAutoAudioAlign.Test
{
    class TempDir : IDisposable
    {
        public readonly DirectoryInfo dir;

        static Random rnd = new Random(DateTime.Now.Millisecond ^ typeof(TempDir).GetHashCode());

        public TempDir()
        {
            lock (rnd)
            {
                var id = new byte[4];
                rnd.NextBytes(id);
                var idHex = id.Hex();
                dir = new DirectoryInfo(Path.Combine(Path.GetTempPath(), idHex));
                dir.Create();
            }
        }

        public void Dispose()
        {
            dir.Refresh();
            if (dir.Exists)
                dir.Delete(true);
        }
    }
}
