using System;
using System.Windows;

namespace SystemIntelligence.PixelMini;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        var application = new Application
        {
            ShutdownMode = ShutdownMode.OnMainWindowClose
        };

        var layoutSmokeTest = Array.IndexOf(args, "--smoke-layout") >= 0;
        application.Run(new PixelWindow(layoutSmokeTest, Array.IndexOf(args, "--visual-check") >= 0));
    }
}
