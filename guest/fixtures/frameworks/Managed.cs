using System;

sealed class Counter
{
    public static int Initializations;
    public static string Prefix;
    public int Value;
    public string Name;
    public readonly int Original;
    static Counter()
    {
#if MANAGED_CCTOR_FAILURE
        int zero = 0;
        Console.WriteLine(1 / zero);
#else
        Initializations++; Prefix = "ação ";
#endif
    }
    public Counter(int value, string name) { Value = value; Original = value; Name = name; }
    public int Add(int amount) { Value += amount; return Value; }
}

class ManagedContract
{
    static int Main()
    {
#if MANAGED_NULL
        Counter missing = null;
        Console.WriteLine(missing.Value);
#elif MANAGED_BOUNDS
        int[] data = new int[2];
        int index = 2;
        Console.WriteLine(data[index]);
#elif MANAGED_NEGATIVE_INDEX
        string[] data = new string[1];
        int index = -1;
        Console.WriteLine(data[index]);
#elif MANAGED_OBJECT_QUOTA
        for (int i = 0; i < 40; i++) new Counter(i, "quota");
#elif MANAGED_ARRAY_QUOTA
        for (int i = 0; i < 20; i++) { int[] data = new int[i]; Console.WriteLine(data.Length); }
#elif MANAGED_STRING_QUOTA
        string value = "a";
        for (int i = 0; i < 70; i++) value = string.Concat(value, "b");
        Console.WriteLine(value);
#elif MANAGED_LONG_ARRAY
        int[] data = new int[257];
        Console.WriteLine(data.Length);
#elif MANAGED_NULL_CALL
        Counter missing = null;
        Console.WriteLine(missing.Add(1));
#else
        Counter first = new Counter(7, "primeiro");
        Counter second = new Counter(11, "segundo");
        if (first.Add(5) != 12 || first.Value != 12 || second.Value != 11 || first.Original != 7) return 1;
        if (Counter.Initializations != 1 || first.Name != "primeiro" || second.Name != "segundo") return 2;
        int[] numbers = new int[10];
        int total = 0;
        for (int i = 0; i < numbers.Length; i++) { numbers[i] = i + 1; total += numbers[i]; }
        if (total != 55) return 3;
        string[] words = new string[3];
        words[0] = Counter.Prefix; words[1] = "e "; words[2] = "Unicode \uD83C\uDF89";
        string joined = string.Concat(words[0], words[1], words[2]);
        if (joined != "ação e Unicode \uD83C\uDF89" || joined.Length != 17) return 4;
        if (!joined.Equals("ação e Unicode \uD83C\uDF89") || string.Equals(joined, "wrong")) return 5;
        if (!string.IsNullOrEmpty(null) || !string.IsNullOrEmpty("") || string.IsNullOrEmpty(joined)) return 6;
        object[] boxed = new object[3];
        boxed[0] = total; boxed[1] = true; boxed[2] = joined;
        if ((int)boxed[0] != 55 || !(bool)boxed[1] || (int)boxed[0] + first.Value != 67) return 7;
        object one = (object)7, two = (object)7, same = one;
        if (one == two || one != same) return 8;
        if (words[0] == null || new int[0].Length != 0) return 9;
        Console.WriteLine(joined);
        Console.WriteLine(boxed[0]); Console.WriteLine(boxed[1]); Console.WriteLine(boxed[2]);
        Console.WriteLine(first); Console.WriteLine(numbers);
        Console.WriteLine(first.Value); Console.WriteLine(second.Value); Console.WriteLine(Counter.Initializations);
        Console.WriteLine("MANAGED_OBJECTS_ARRAYS_PASS");
        return 0;
#endif
        return 99;
    }
}
