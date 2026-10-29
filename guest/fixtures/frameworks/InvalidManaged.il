// Deliberately invalid managed code. Compile for MiniNT negative tests only.
// Never execute this fixture on the host.
.assembly extern mscorlib { .ver 4:0:0:0 }
.assembly InvalidManaged { }
.module InvalidManaged.exe
.class private auto ansi sealed First extends [mscorlib]System.Object {
    .field public initonly int32 ReadOnly
    .method public hidebysig specialname rtspecialname instance void .ctor() cil managed {
        .maxstack 2
        ldarg.0
        call instance void [mscorlib]System.Object::.ctor()
        ldarg.0
        ldc.i4.7
        stfld int32 First::ReadOnly
        ret
    }
}
.class private auto ansi sealed Second extends [mscorlib]System.Object {
    .field public int32 Other
    .method public hidebysig specialname rtspecialname instance void .ctor() cil managed {
        .maxstack 1
        ldarg.0
        call instance void [mscorlib]System.Object::.ctor()
        ret
    }
}
.class private auto ansi sealed InvalidManaged extends [mscorlib]System.Object {
    .method private hidebysig static int32 Main() cil managed {
        .entrypoint
        .maxstack 2
        newobj instance void First::.ctor()
        ldc.i4.s 99
        // Only the owning constructor may write this field.
        stfld int32 First::ReadOnly
        ldc.i4.0
        ret
    }
}
