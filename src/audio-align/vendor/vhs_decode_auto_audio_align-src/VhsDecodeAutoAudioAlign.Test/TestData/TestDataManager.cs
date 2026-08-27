using System;
using System.IO;
using System.Linq;

namespace VhsDecodeAutoAudioAlign.Test.TestData
{
    static class TestDataManager
    {
        static DirectoryInfo solutionDir;
        static DirectoryInfo SolutionDir
        {
            get
            {
                if (solutionDir == null)
                {
                    var loc = typeof(TestDataManager).Assembly.Location;
                    var f = new FileInfo(loc);
                    var solution = f.Directory;

                    while (solution.GetFiles().Any(ff => ff.Name.ToLower().EndsWith(".sln")) == false)
                    {
                        solution = solution.Parent;
                        if (solution == null)
                            throw new DirectoryNotFoundException("Cannot find solution dir");
                    }

                    solutionDir = solution;
                }

                return solutionDir;
            }
        }

        static DirectoryInfo TestDataDir => SolutionDir.Dir("VhsDecodeAutoAudioAlign.Test").Dir("TestData");

        static FileInfo MustExist(string name)
        {
            var f = TestDataDir.File(name);
            if (f.Exists == false)
                throw new FileNotFoundException(f.FullName);
            return f;
        }

        public static FileInfo DemoTbcJson => MustExist("demo.tbc.json");
    }
}
