.assembly extern mscorlib { .ver 4:0:0:0 }
.assembly InvalidForms {}
.module InvalidForms.exe
.class public FormsBad extends [mscorlib]System.Object {
    .method public static void Wrong() cil managed { .maxstack 0 ret }
    .method public static void Main() cil managed {
        .entrypoint
        .maxstack 2
        ldnull
        ldftn void FormsBad::Wrong()
        newobj instance void [mscorlib]System.EventHandler::.ctor(object, native int)
        pop
        ret
    }
}
