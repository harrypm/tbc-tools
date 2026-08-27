using System;
using System.IO;

namespace VhsDecodeAutoAudioAlign
{
    public interface ILog
    {
        void That(string text);
        void That(Exception ex);
    }

    public class ConsoleLog : ILog
    {
        readonly TextWriter wrt;

        public ConsoleLog()
            :this(Console.Out)
        {
        }

        public ConsoleLog(TextWriter wrt)
        {
            this.wrt = wrt ?? throw new ArgumentNullException(nameof(wrt));
        }

        static string ToPrefix(DateTime ts)
        {
            return "[" + ts.ToString("HH:mm:ss.fff") + "] ";
        }

        void Write(string prefix, string line)
        {
            wrt.Write(prefix);
            wrt.WriteLine(line);
        }

        public void That(string text)
        {
            var p = ToPrefix(DateTime.Now);
            Write(p, text);
        }

        void Write(string prefix, Exception ex)
        {
            if (ex == null)
                return;

            Write(prefix, ex.GetType() + " - " + ex.Message);
            foreach (var l in ex.StackTrace.Split('\n', '\r'))
                Write(prefix, l);

            if (ex is AggregateException aggr)
                foreach (var a in aggr.InnerExceptions)
                    Write(prefix, a);

            Write(prefix, ex.InnerException);
        }

        public void That(Exception ex)
        {
            var p = ToPrefix(DateTime.Now);
            Write(p, ex);
        }
    }

}
