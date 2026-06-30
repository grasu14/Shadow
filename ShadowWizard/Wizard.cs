using System;
using System.Drawing;
using System.Windows.Forms;
using System.IO;
using System.Net;
using System.Diagnostics;
using System.Reflection;

namespace ShadowWizard
{
    public class WizardForm : Form
    {
        private Panel pageTerms;
        private Panel pageProgress;
        private Button btnNext;
        private Button btnCancel;
        private Label lblTitle;
        private RichTextBox rtbTerms;
        private ProgressBar progressBar;
        private Label lblStatus;
        private string installDir;

        public WizardForm()
        {
            this.Text = "Shadow Setup Wizard";
            this.Size = new Size(500, 400);
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.StartPosition = FormStartPosition.CenterScreen;

            // Default Install Dir (mimicking the path you requested for extraction)
            installDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Desktop), "Shadow", "bin", "Debug");

            lblTitle = new Label();
            lblTitle.Text = "Shadow Setup";
            lblTitle.Font = new Font("Segoe UI", 16, FontStyle.Bold);
            lblTitle.Location = new Point(20, 10);
            lblTitle.AutoSize = true;
            this.Controls.Add(lblTitle);

            btnCancel = new Button();
            btnCancel.Text = "Cancel";
            btnCancel.Location = new Point(400, 320);
            btnCancel.Click += (s, e) => { this.Close(); };
            this.Controls.Add(btnCancel);

            btnNext = new Button();
            btnNext.Text = "Next >";
            btnNext.Location = new Point(310, 320);
            btnNext.Click += BtnNext_Click;
            this.Controls.Add(btnNext);

            InitializeTermsPage();
            InitializeProgressPage();

            pageTerms.Visible = true;
            pageProgress.Visible = false;
        }

        private void InitializeTermsPage()
        {
            pageTerms = new Panel();
            pageTerms.Location = new Point(20, 50);
            pageTerms.Size = new Size(440, 250);
            this.Controls.Add(pageTerms);

            Label lblDesc = new Label();
            lblDesc.Text = "Please read the following important information before continuing.";
            lblDesc.AutoSize = true;
            lblDesc.Location = new Point(0, 0);
            pageTerms.Controls.Add(lblDesc);

            rtbTerms = new RichTextBox();
            rtbTerms.ReadOnly = true;
            rtbTerms.Location = new Point(0, 25);
            rtbTerms.Size = new Size(440, 220);
            rtbTerms.Text = @"SHADOW - MODULAR SYSTEM PRIVACY AND CLEANUP UTILITY
===================================================

Made by TQk.z

DESCRIPTION:
Shadow is a modular system privacy and cleanup utility for Windows. It is designed to safely clear local application caches, browser histories, and specific system execution logs.

LIABILITY DISCLAIMER:
We do not cover any damages to reputation that this tool might give, use it at your own will.

GITHUB DISCLAIMER:
The direct executable is available on GitHub (https://github.com/grasu14/Shadow).

This wizard will automatically install Microsoft Visual C++ Redistributable if needed, and extract the compiled Shadow executable.";
            pageTerms.Controls.Add(rtbTerms);
        }

        private void InitializeProgressPage()
        {
            pageProgress = new Panel();
            pageProgress.Location = new Point(20, 50);
            pageProgress.Size = new Size(440, 250);
            this.Controls.Add(pageProgress);

            lblStatus = new Label();
            lblStatus.Text = "Ready to install...";
            lblStatus.AutoSize = true;
            lblStatus.Location = new Point(0, 100);
            pageProgress.Controls.Add(lblStatus);

            progressBar = new ProgressBar();
            progressBar.Location = new Point(0, 120);
            progressBar.Size = new Size(440, 25);
            pageProgress.Controls.Add(progressBar);
        }

        private async void BtnNext_Click(object sender, EventArgs e)
        {
            if (pageTerms.Visible)
            {
                pageTerms.Visible = false;
                pageProgress.Visible = true;
                btnNext.Enabled = false;
                btnCancel.Enabled = false;

                await StartInstallation();
            }
            else
            {
                this.Close();
            }
        }

        private async System.Threading.Tasks.Task StartInstallation()
        {
            try
            {
                ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;

                lblStatus.Text = "Downloading Visual C++ Redistributable...";
                progressBar.Value = 20;

                string vcppUrl = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
                string tempFile = Path.Combine(Path.GetTempPath(), "vc_redist.x64.exe");

                using (WebClient client = new WebClient())
                {
                    await client.DownloadFileTaskAsync(new Uri(vcppUrl), tempFile);
                }

                lblStatus.Text = "Installing Visual C++ Redistributable...";
                progressBar.Value = 50;

                Process p = new Process();
                p.StartInfo.FileName = tempFile;
                p.StartInfo.Arguments = "/install /quiet /norestart";
                p.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;
                p.Start();
                p.WaitForExit();

                lblStatus.Text = "Extracting Shadow.exe...";
                progressBar.Value = 80;

                if (!Directory.Exists(installDir))
                {
                    Directory.CreateDirectory(installDir);
                }

                ExtractResource("Shadow.exe", Path.Combine(installDir, "Shadow.exe"));
                ExtractResource("targets.txt", Path.Combine(installDir, "targets.txt"));

                lblStatus.Text = "Installation Complete!";
                progressBar.Value = 100;
                btnNext.Text = "Finish";
                btnNext.Enabled = true;
            }
            catch (Exception ex)
            {
                lblStatus.Text = "Error: " + ex.Message;
                btnCancel.Enabled = true;
            }
        }

        private void ExtractResource(string resourceName, string outPath)
        {
            Assembly assembly = Assembly.GetExecutingAssembly();
            // Csc.exe resource embeds usually just use the filename as the resource name.
            using (Stream stream = assembly.GetManifestResourceStream(resourceName))
            {
                if (stream == null) return;
                using (FileStream fileStream = new FileStream(outPath, FileMode.Create))
                {
                    stream.CopyTo(fileStream);
                }
            }
        }
    }

    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new WizardForm());
        }
    }
}
