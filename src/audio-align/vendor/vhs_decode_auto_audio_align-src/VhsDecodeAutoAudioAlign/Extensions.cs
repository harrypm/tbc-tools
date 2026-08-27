using System;
using System.IO;

namespace VhsDecodeAutoAudioAlign
{
    static class Extensions
    {
        public static FileInfo File(this DirectoryInfo me, string name)
        {
            return new FileInfo(Path.Combine(me.FullName, name));
        }

        public static DirectoryInfo Dir(this DirectoryInfo me, string name)
        {
            return new DirectoryInfo(Path.Combine(me.FullName, name));
        }

        public static string PositionStringSafe(this Stream me)
        {
            return me.CanSeek ? me.Position.ToString() : "?";
        }

        public static string Hex(this byte[] data)
        {
            return BitConverter.ToString(data).Replace("-", "").ToLower();
        }
    }
}
