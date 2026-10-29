using System;
class Negative
{
#if CIL_DIVZERO
    static int Main(string[] args) { return 7/args.Length; }
#elif CIL_LOOP
    static void Main() { while(true) {} }
#elif CIL_DEPTH
    static int Recursive(int n) { return n==0?0:Recursive(n-1); }
    static int Main() { return Recursive(100); }
#elif CIL_I8
    static void Main() { Console.WriteLine(4294967296L); }
#elif CIL_FILE
    static void Main() { System.IO.File.WriteAllText("C:\\App\\must-not-exist.txt","unsupported"); }
#elif CIL_PINVOKE
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern bool Beep(uint frequency,uint duration);
    static void Main() { Beep(440,1); }
#elif CIL_WINFORMS
    static void Main() { new System.Windows.Forms.WebBrowser(); }
#elif CIL_WPF
    static void Main() { new System.Windows.Application().Run(); }
#elif CIL_STATIC
    static int initializations;
    static Negative() { initializations++; Console.WriteLine("STATIC_CTOR_RUN"); }
    static int Main() { if (initializations != 1) return 1; Console.WriteLine("STATIC_INITIALIZER_PASS"); return 0; }
#elif CIL_OUTPUT
    static void Main() { while(true) Console.WriteLine("0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"); }
#elif CIL_CONTROL
    static void Main() { Console.WriteLine("\u001b[2J"); }
#else
    static int Main() { return 0; }
#endif
}
