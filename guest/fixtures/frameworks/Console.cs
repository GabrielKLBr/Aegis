using System;
class ConsoleContract
{
    static int Sum(int n) { int result=0; for(int i=1;i<=n;i++) result+=i; return result; }
    static int Factorial(int n) { return n<=1?1:n*Factorial(n-1); }
    static int Main(string[] args)
    {
        Console.WriteLine("MiniNT CIL console: ação e Unicode \uD83C\uDF89");
        Console.WriteLine(Sum(10));
        Console.WriteLine(Factorial(5));
        Console.WriteLine((14<<2)|3);
        Console.WriteLine(args.Length==0);
        Console.WriteLine(-2147483647-1);
        Console.WriteLine("CIL_CONSOLE_PASS");
        return 0;
    }
}
