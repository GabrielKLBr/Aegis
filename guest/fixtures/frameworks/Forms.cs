using System;
using System.Windows.Forms;

// Owned acceptance fixture, never an imported app executed on the host.
class FormsAcceptance {
    static Form form;
    static Button button;
    static TextBox edit;
    static int clicks, disposed, failures;
    static void Check(bool value) { if (!value) failures++; }
    static void Click(object sender, EventArgs args) {
        Check(sender == button && args != null);
        clicks++;
        edit.Text = "ação 🎉";
#if FORMS_INPUT
        form.Close();
#endif
    }
    static void Disposed(object sender, EventArgs args) { disposed++; }
    sealed class Receiver {
        public int Count;
        public void Click(object sender, EventArgs args) { Count++; }
    }
    [STAThread]
    static int Main() {
        form = new Form();
        form.Text = "Aegis WinForms acceptance";
        form.Width = 360; form.Height = 240;
        form.Disposed += Disposed;
#if FORMS_RUN
        Application.Run(form);
        Check(disposed == 1 && form.IsDisposed);
        Console.WriteLine("WINFORMS_RUN_PASS");
#elif FORMS_INPUT
        button = new Button(); button.Text = "Native input";
        edit = new TextBox(); edit.Text = "Input ready"; edit.Top = 52; edit.Width = 240;
        form.Controls.Add(button); form.Controls.Add(edit); button.Click += Click;
        Application.Run(form);
        Check(clicks == 1 && disposed == 1 && form.IsDisposed && edit.IsDisposed);
        Console.WriteLine("WINFORMS_NATIVE_INPUT_PASS");
#elif FORMS_NULL_RUN
        Application.Run((Form)null);
#elif FORMS_QUOTA
        for (int i = 0; i < 9; i++) form.Disposed += Disposed;
#elif FORMS_CALLBACK_FAILURE
        button = new Button();
        button.Click += delegate { int zero = failures; Console.WriteLine(5 / zero); };
        form.Controls.Add(button);
        form.Show(); button.PerformClick();
#elif FORMS_TEXT_QUOTA
        form.Show();
        form.Text = "................................................................" +
            "................................................................" +
            "................................................................" +
            "................................................................";
#else
        button = new Button(); button.Text = "Executar";
        button.Left = 12; button.Top = 12; button.Width = 120; button.Height = 28;
        edit = new TextBox(); edit.Text = "initial"; edit.Top = 52; edit.Width = 240;
        Label label = new Label(); label.Text = "Interface gerenciada"; label.Top = 90; label.Width = 240;
        form.Controls.Add(button); form.Controls.Add(edit); form.Controls.Add(label);
        Receiver receiver = new Receiver();
        button.Click += Click; button.Click += receiver.Click;
        button.Click += Click; button.Click -= Click;
        form.Show(); Application.DoEvents();
        Check(form.Visible && form.Text == "Aegis WinForms acceptance");
        Check(button.Width == 120 && button.Height == 28 && button.Left == 12 && button.Top == 12);
        button.PerformClick(); Application.DoEvents();
        Check(clicks == 1 && receiver.Count == 1 && edit.Text == "ação 🎉");
        form.Hide(); Check(!form.Visible); form.Show();
        button.Click -= receiver.Click;
        button.PerformClick();
        Check(clicks == 2 && receiver.Count == 1);
        form.Close(); form.Dispose();
        Check(disposed == 1 && form.IsDisposed && button.IsDisposed && edit.IsDisposed && label.IsDisposed);
        Console.WriteLine("WINFORMS_CONTROLS_PASS");
#endif
        Console.WriteLine(failures);
        return failures == 0 ? 0 : 1;
    }
}
