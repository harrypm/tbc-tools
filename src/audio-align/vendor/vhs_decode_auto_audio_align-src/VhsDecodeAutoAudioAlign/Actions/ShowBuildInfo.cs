using System;
namespace VhsDecodeAutoAudioAlign.Actions
{
    class ShowBuildInfo : Binah.IAction<object>
    {
        readonly ILog log;

        public ShowBuildInfo(ILog log)
        {
            this.log = log;
        }

        public string Switch => "show-build-info";

        public string Info => "Shows information on the build of this version";

        public void Run(object args, string[] unconsumend)
        {
            log.That(About.LongName + " v" + BuildInfo.VersionSemantic);
            log.That("Build commit: " + BuildInfo.Commit);
            log.That("Build job: " + BuildInfo.BuildJob);
            log.That("Build time: " + BuildInfo.BuildTime);
        }
    }
}
