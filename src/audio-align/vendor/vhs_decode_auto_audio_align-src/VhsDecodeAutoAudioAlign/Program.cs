using System;
using System.IO;
using VhsDecodeAutoAudioAlign.TbcJson;

namespace VhsDecodeAutoAudioAlign
{
    class MainClass
    {
        public static int Main(string[] args)
        {
            var log = new ConsoleLog(Console.Error);
            try
            {
                var app = new Binah.ActionApp(Console.Error.WriteLine) 
                {
                    AppName = About.LongName + " v" + BuildInfo.VersionSemantic,
                    AppInfo = About.Info,
                };
                app.Add(new Actions.ShowBuildInfo(log));
                app.Add(new Actions.GenerateDriftCsv(log));
                app.Add(new Actions.StreamAlign(log));
                app.Run(args);

                return 0;
            }
            catch (Exception x)
            {

                var tg = x as System.Reflection.TargetInvocationException;
                log.That(tg != null ? tg.InnerException : x);
                return -1;
            }
        }
    }
}
