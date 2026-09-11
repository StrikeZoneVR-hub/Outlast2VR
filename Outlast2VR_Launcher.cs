using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Media;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        bool preview = args.Length > 0 && (args[0] == "--preview" || args[0] == "--snapshot");
        LauncherForm form = new LauncherForm(preview);
        if (args.Length > 1 && args[0] == "--snapshot")
        {
            form.Show(); form.PrepareSnapshot(); Application.DoEvents();
            using (Bitmap image = new Bitmap(form.ClientSize.Width, form.ClientSize.Height))
            { form.DrawToBitmap(image, new Rectangle(Point.Empty, form.ClientSize)); image.Save(Path.GetFullPath(args[1]), ImageFormat.Png); }
            form.Close(); return;
        }
        Application.Run(form);
    }
}

enum LauncherPage { Launch, Controls, Diagnostics, Options }

public class LauncherForm : Form
{
    const string MusicAlias = "Outlast2VRMenuMusic";
    const string BuildName = "BETA 1  •  PF17 NATIVE VR VIEW";
    readonly string gameDir, exePath, preferencesPath;
    readonly bool previewMode;
    readonly Random random = new Random();
    readonly DateTime bootStarted = DateTime.UtcNow;
    readonly Timer animationTimer = new Timer();
    readonly Button[] navButtons = new Button[4];
    readonly Panel[] pages = new Panel[4];
    Image background;
    RadioButton defaultOpenXR, steamOpenXR, colorSrgb, colorLegacy, graphicsSafe, graphicsKeep;
    CheckBox reduceFlashes;
    Label diagnosticsText, mainRuntime, statusLine;
    Button play, musicButton;
    string musicPath;
    bool musicPlaying, bootComplete, launchQueued;
    DateTime launchAt, nextFigureAt, figureUntil;
    Point mouse = new Point(580, 380);
    float parallaxX, parallaxY;
    SoundPlayer bootSound;
    MemoryStream bootWave;
    bool fullscreen = true;

    [DllImport("winmm.dll", CharSet=CharSet.Auto)]
    static extern int mciSendString(string command, StringBuilder result, int resultLength, IntPtr callback);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    static extern bool WritePrivateProfileString(string section, string key, string value, string fileName);

    public LauncherForm(bool preview)
    {
        previewMode = preview;
        gameDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        exePath = Path.Combine(gameDir, "Outlast2.exe");
        preferencesPath = Path.Combine(gameDir, "Outlast2VR_launcher.ini");
        Text = "Outlast 2 VR";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1180, 760);
        MinimumSize = new Size(960, 680);
        FormBorderStyle = FormBorderStyle.None; WindowState = FormWindowState.Maximized; MaximizeBox = true;
        BackColor = Color.Black; ForeColor = Color.WhiteSmoke; Font = new Font("Segoe UI", 9f);
        DoubleBuffered = true; KeyPreview = true;
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch {}
        LoadBackground(); LoadPreferences(); BuildNavigation(); BuildLaunchPage(); BuildControlsPage(); BuildDiagnosticsPage(); BuildOptionsPage();
        foreach(Control control in Controls)control.Tag=control.Location;
        Resize += delegate { LayoutForScreen(); };
        bootComplete = false; ShowPage(LauncherPage.Launch); SetPageVisibility(false);
        musicPath = FindMenuMusic(); UpdateMusicButton();
        nextFigureAt = DateTime.UtcNow.AddSeconds(38 + random.Next(35));
        animationTimer.Interval = 33; animationTimer.Tick += AnimationTick; animationTimer.Start();
        MouseMove += TrackMouse; foreach (Control control in Controls) control.MouseMove += TrackMouse;
        Shown += delegate { LayoutForScreen(); StartBootSound(); StartMusic(); };
        FormClosed += delegate { StopMusic(); animationTimer.Stop(); if (bootSound != null) bootSound.Stop(); };
        KeyDown += LauncherKeyDown;
        if (!previewMode && !File.Exists(exePath))
        {
            MessageBox.Show("Outlast2.exe was not found. Place Outlast2VR.exe in the game's Binaries\\Win64 folder.", "Outlast 2 VR", MessageBoxButtons.OK, MessageBoxIcon.Error);
            Shown += delegate { Close(); };
        }
    }

    public void PrepareSnapshot() { bootComplete = true; SetPageVisibility(true); mouse = new Point(430,445); Invalidate(true); }

    void LayoutForScreen()
    {
        int dx=Math.Max(0,(ClientSize.Width-1180)/2),dy=Math.Max(0,(ClientSize.Height-760)/2);
        foreach(Control control in Controls)if(control.Tag is Point){Point p=(Point)control.Tag;control.Location=new Point(p.X+dx,p.Y+dy);}
    }

    void SetFullscreen(bool enabled)
    {
        fullscreen=enabled;
        if(enabled){FormBorderStyle=FormBorderStyle.None;WindowState=FormWindowState.Maximized;}
        else{WindowState=FormWindowState.Normal;FormBorderStyle=FormBorderStyle.Sizable;ClientSize=new Size(1180,760);CenterToScreen();}
        LayoutForScreen();
    }

    void BuildNavigation()
    {
        string[] names = { "PLAY", "CONTROLS", "VR CHECK", "OPTIONS" };
        for (int i=0;i<names.Length;++i)
        {
            Button button=MakeButton(names[i],new Point(34+i*138,24),new Size(126,36),9f); int page=i;
            button.Click += delegate { if (bootComplete) ShowPage((LauncherPage)page); }; Controls.Add(button); navButtons[i]=button;
        }
        Label build=MakeLabel(BuildName,9f,FontStyle.Bold,Color.FromArgb(255,188,77),ContentAlignment.MiddleRight);
        build.Location=new Point(625,24); build.Size=new Size(521,36); Controls.Add(build);
    }

    void BuildLaunchPage()
    {
        Panel page=MakePanel(new Point(746,286),new Size(400,424)); pages[0]=page; Controls.Add(page);
        Label eyebrow=MakeLabel("MURKOFF FIELD SYSTEM  //  CAMERA 01",8.5f,FontStyle.Bold,Color.FromArgb(210,225,212),ContentAlignment.MiddleLeft);
        eyebrow.Location=new Point(24,18); eyebrow.Size=new Size(360,22); page.Controls.Add(eyebrow);
        Label title=MakeLabel("ENTER TEMPLE GATE",22f,FontStyle.Bold,Color.White,ContentAlignment.MiddleLeft);
        title.Location=new Point(22,43); title.Size=new Size(365,44); page.Controls.Add(title);
        Label desc=MakeLabel("Outlast 2 rebuilt for PC VR.\nTracked hands, native animation and gamepad-free play.",10f,FontStyle.Regular,Color.FromArgb(198,206,198),ContentAlignment.TopLeft);
        desc.Location=new Point(25,88); desc.Size=new Size(350,44); page.Controls.Add(desc);
        Panel rule=new Panel(); rule.BackColor=Color.FromArgb(110,47,31); rule.Location=new Point(24,136); rule.Size=new Size(350,1); page.Controls.Add(rule);
        mainRuntime=MakeLabel("OPENXR  //  "+FriendlyRuntime(GetDefaultRuntime()),10f,FontStyle.Bold,Color.FromArgb(153,226,174),ContentAlignment.MiddleLeft);
        mainRuntime.Location=new Point(25,146); mainRuntime.Size=new Size(350,24); page.Controls.Add(mainRuntime);
        statusLine=MakeLabel("BETA 1 GAMEPLAY CORE",9f,FontStyle.Bold,Color.FromArgb(255,188,77),ContentAlignment.MiddleLeft);
        statusLine.Location=new Point(25,174); statusLine.Size=new Size(350,22); page.Controls.Add(statusLine);
        play=MakeButton("PLAY VR",new Point(24,207),new Size(350,65),18f); play.FlatAppearance.BorderSize=2;
        play.MouseEnter+=delegate { play.BackColor=Color.FromArgb(32,92,53); play.FlatAppearance.BorderColor=Color.FromArgb(132,255,168); Invalidate(); };
        play.MouseLeave+=delegate { play.BackColor=Color.FromArgb(11,17,13); play.FlatAppearance.BorderColor=Color.FromArgb(89,104,90); Invalidate(); };
        play.Click+=delegate { QueueLaunch(); }; page.Controls.Add(play);
        Label hint=MakeLabel("Headset on  •  controllers awake  •  press ENTER",8.5f,FontStyle.Regular,Color.FromArgb(175,188,177),ContentAlignment.MiddleCenter);
        hint.Location=new Point(24,275); hint.Size=new Size(350,22); page.Controls.Add(hint);
        musicButton=MakeButton("MUSIC",new Point(24,305),new Size(168,38),8.5f); musicButton.Click+=delegate { ToggleMusic(); }; page.Controls.Add(musicButton);
        Button check=MakeButton("RUN VR CHECK",new Point(206,305),new Size(168,38),8.5f); check.Click+=delegate { ShowPage(LauncherPage.Diagnostics); RefreshDiagnostics(); }; page.Controls.Add(check);
        Label legal=MakeLabel("Unofficial fan-made PC VR modification.\nOriginal copy of Outlast 2 required.",8f,FontStyle.Regular,Color.FromArgb(135,145,136),ContentAlignment.BottomLeft);
        legal.Location=new Point(25,357); legal.Size=new Size(350,42); page.Controls.Add(legal); AcceptButton=play;
    }

    void BuildControlsPage()
    {
        Panel page=MakePanel(new Point(80,70),new Size(1020,640)); pages[1]=page; Controls.Add(page);
        AddPageTitle(page,"QUEST / TOUCH CONTROLS","PHYSICAL INPUT MAP  //  PUBLIC BETA");
        ControllerDiagram diagram=new ControllerDiagram(); diagram.Location=new Point(24,92); diagram.Size=new Size(972,374); diagram.BackColor=Color.FromArgb(8,13,10); page.Controls.Add(diagram);
        Panel words=MakeGroup(new Point(28,474),new Size(964,128)); words.BorderStyle=BorderStyle.FixedSingle; page.Controls.Add(words);
        Label leftWords=MakeLabel("1  Left stick: Move; control movable objects after pressing X\n2  Left-stick click: Sprint\n3  Right stick left/right: Turn\n4  Right stick up/down: Camcorder zoom\n5  A: Jump\n6  B: Crouch/crawl",7.5f,FontStyle.Bold,Color.FromArgb(224,231,222),ContentAlignment.TopLeft);
        leftWords.Location=new Point(12,8);leftWords.Size=new Size(304,112);words.Controls.Add(leftWords);
        Label rightWords=MakeLabel("7  X: Interact, pick up items, open doors, use switches,\n    or start moving objects\n8  Y: Reload camcorder battery\n9  Hold Y: Apply a bandage\n10  Right grip: Raise/lower camcorder",7.5f,FontStyle.Bold,Color.FromArgb(224,231,222),ContentAlignment.TopLeft);
        rightWords.Location=new Point(326,8);rightWords.Size=new Size(310,112);words.Controls.Add(rightWords);
        Label otherWords=MakeLabel("11  Right-stick click: Toggle night vision\n12  Y + right-stick click: Toggle microphone\n13  X + Y: Open recordings/inventory\n14  A + B: Look behind\n15  Menu button: Pause\n16  Both stick clicks: Open VR settings",7.5f,FontStyle.Bold,Color.FromArgb(224,231,222),ContentAlignment.TopLeft);
        otherWords.Location=new Point(646,8);otherWords.Size=new Size(306,112);words.Controls.Add(otherWords);
        Label note=MakeLabel("Native animations remain active during interactions and scripted sequences.   F11 or Esc exits fullscreen.",8.3f,FontStyle.Regular,Color.FromArgb(168,182,170),ContentAlignment.MiddleCenter);
        note.Location=new Point(28,607); note.Size=new Size(964,22); page.Controls.Add(note);
    }

    void BuildDiagnosticsPage()
    {
        Panel page=MakePanel(new Point(150,104),new Size(880,566)); pages[2]=page; Controls.Add(page);
        AddPageTitle(page,"VR CHECK","OPENXR AND INSTALLATION DIAGNOSTICS");
        diagnosticsText=MakeLabel("",10f,FontStyle.Regular,Color.FromArgb(220,228,219),ContentAlignment.TopLeft); diagnosticsText.Font=new Font("Consolas",10f);
        diagnosticsText.Location=new Point(40,105); diagnosticsText.Size=new Size(800,330); page.Controls.Add(diagnosticsText);
        Button refresh=MakeButton("REFRESH CHECK",new Point(40,456),new Size(210,50),10f); refresh.Click+=delegate { RefreshDiagnostics(); }; page.Controls.Add(refresh);
        Label truth=MakeLabel("Live headset and controller tracking is verified by OpenXR when the game session starts.\nThis screen does not claim a live connection before a session exists.",8.5f,FontStyle.Regular,Color.FromArgb(160,175,162),ContentAlignment.MiddleLeft);
        truth.Location=new Point(280,450); truth.Size=new Size(560,66); page.Controls.Add(truth);
    }

    void BuildOptionsPage()
    {
        Panel page=MakePanel(new Point(135,92),new Size(910,600)); pages[3]=page; Controls.Add(page);
        AddPageTitle(page,"OPTIONS","RUNTIME, COMFORT AND VISUAL SAFETY");
        page.Controls.Add(SectionTitle("OPENXR RUNTIME",112));
        Panel runtimeGroup=MakeGroup(new Point(40,140),new Size(400,70));
        defaultOpenXR=MakeRadio("Default OpenXR / Virtual Desktop (recommended)",new Point(4,5),new Size(390,28));
        steamOpenXR=MakeRadio("Force SteamVR OpenXR for this launch",new Point(4,37),new Size(390,28)); runtimeGroup.Controls.Add(defaultOpenXR); runtimeGroup.Controls.Add(steamOpenXR); page.Controls.Add(runtimeGroup);
        page.Controls.Add(SectionTitle("HEADSET COLOR TRANSFER",224));
        Panel colorGroup=MakeGroup(new Point(40,252),new Size(400,70));
        colorSrgb=MakeRadio("sRGB — recommended headset color",new Point(4,5),new Size(390,28));
        colorLegacy=MakeRadio("Legacy UNORM — troubleshooting only",new Point(4,37),new Size(390,28)); colorGroup.Controls.Add(colorSrgb); colorGroup.Controls.Add(colorLegacy); page.Controls.Add(colorGroup);
        page.Controls.Add(SectionTitle("GRAPHICS PRESET",336));
        Panel graphicsGroup=MakeGroup(new Point(40,364),new Size(400,70));
        graphicsSafe=MakeRadio("VR-safe — no motion blur / temporal AA",new Point(4,5),new Size(390,28));
        graphicsKeep=MakeRadio("Keep current game graphics — advanced",new Point(4,37),new Size(390,28)); graphicsGroup.Controls.Add(graphicsSafe); graphicsGroup.Controls.Add(graphicsKeep); page.Controls.Add(graphicsGroup);
        page.Controls.Add(SectionTitle("ACCESSIBILITY",448));
        reduceFlashes=new CheckBox(); reduceFlashes.Text="Reduce flashes and disable hidden horror effects"; reduceFlashes.Location=new Point(44,481); reduceFlashes.Size=new Size(390,30);
        reduceFlashes.ForeColor=Color.White; reduceFlashes.BackColor=page.BackColor; reduceFlashes.CheckedChanged+=delegate { SavePreferences(); Invalidate(); }; page.Controls.Add(reduceFlashes);
        Panel divide=new Panel(); divide.BackColor=Color.FromArgb(80,62,41); divide.Location=new Point(454,110); divide.Size=new Size(1,416); page.Controls.Add(divide);
        Label recenter=MakeLabel("RECENTER FOR COMFORT",12f,FontStyle.Bold,Color.FromArgb(255,188,77),ContentAlignment.MiddleLeft); recenter.Location=new Point(490,113); recenter.Size=new Size(365,30); page.Controls.Add(recenter);
        Label instructions=MakeLabel("1. Sit or stand in your intended play position.\n\n2. Face straight ahead before launching.\n\n3. Hold both stick clicks (L3 + R3) in gameplay to open the VR settings/recenter controls.\n\nVirtual Desktop: use VDXR with color vibrance and gamma at neutral while comparing visuals.\n\nThe recommended settings preserve the Beta 1 gameplay renderer.",10f,FontStyle.Regular,Color.FromArgb(210,218,208),ContentAlignment.TopLeft);
        instructions.Location=new Point(490,156); instructions.Size=new Size(360,315); page.Controls.Add(instructions);
        Button save=MakeButton("SAVE OPTIONS",new Point(490,486),new Size(360,54),11f); save.Click+=delegate { SavePreferences(); statusLine.Text="OPTIONS SAVED"; ShowPage(LauncherPage.Launch); }; page.Controls.Add(save);
        ApplyPreferencesToControls();
    }

    void AddPageTitle(Panel page,string title,string subtitle)
    {
        Label t=MakeLabel(title,21f,FontStyle.Bold,Color.White,ContentAlignment.MiddleLeft); t.Location=new Point(28,18); t.Size=new Size(page.Width-56,40); page.Controls.Add(t);
        Label s=MakeLabel(subtitle,8.5f,FontStyle.Bold,Color.FromArgb(255,188,77),ContentAlignment.MiddleLeft); s.Location=new Point(30,59); s.Size=new Size(page.Width-60,22); page.Controls.Add(s);
        Panel line=new Panel(); line.BackColor=Color.FromArgb(90,58,38); line.Location=new Point(28,86); line.Size=new Size(page.Width-56,1); page.Controls.Add(line);
    }
    Label SectionTitle(string text,int y) { Label l=MakeLabel(text,11f,FontStyle.Bold,Color.FromArgb(255,188,77),ContentAlignment.MiddleLeft); l.Location=new Point(42,y); l.Size=new Size(390,28); return l; }

    void ShowPage(LauncherPage page)
    {
        for(int i=0;i<pages.Length;++i)
        {
            if(pages[i]!=null) pages[i].Visible=bootComplete&&i==(int)page;
            if(navButtons[i]!=null) { bool active=i==(int)page; navButtons[i].BackColor=active?Color.FromArgb(86,28,16):Color.FromArgb(11,17,13); navButtons[i].FlatAppearance.BorderColor=active?Color.FromArgb(255,111,44):Color.FromArgb(86,101,86); }
        }
        if(bootComplete&&pages[(int)page]!=null){pages[(int)page].Show();pages[(int)page].BringToFront();}
        foreach(Button b in navButtons)if(b!=null)b.BringToFront();
        if(page==LauncherPage.Diagnostics) RefreshDiagnostics(); Invalidate();
    }
    void SetPageVisibility(bool visible)
    {
        if(!visible){foreach(Panel p in pages)if(p!=null)p.Visible=false;}
        else{ShowPage(LauncherPage.Launch);}
        foreach(Button b in navButtons)if(b!=null)b.Visible=visible;
    }

    void AnimationTick(object sender,EventArgs e)
    {
        if(!bootComplete&&(DateTime.UtcNow-bootStarted).TotalMilliseconds>=3300){bootComplete=true;SetPageVisibility(true);}
        float tx=((float)mouse.X/Math.Max(1,ClientSize.Width)-.5f)*13f, ty=((float)mouse.Y/Math.Max(1,ClientSize.Height)-.5f)*8f;
        parallaxX+=(tx-parallaxX)*.08f; parallaxY+=(ty-parallaxY)*.08f;
        if(!reduceFlashesValue&&DateTime.UtcNow>=nextFigureAt){figureUntil=DateTime.UtcNow.AddMilliseconds(900+random.Next(700));nextFigureAt=DateTime.UtcNow.AddSeconds(55+random.Next(75));}
        if(launchQueued&&DateTime.UtcNow>=launchAt){launchQueued=false;LaunchGameCore();} Invalidate();
    }
    bool reduceFlashesValue { get { return reduceFlashes!=null&&reduceFlashes.Checked; } }
    void TrackMouse(object sender,MouseEventArgs e){Control source=sender as Control; mouse=source==null?e.Location:PointToClient(source.PointToScreen(e.Location));}

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        Graphics g=e.Graphics; g.SmoothingMode=SmoothingMode.AntiAlias; g.InterpolationMode=InterpolationMode.HighQualityBicubic;
        if(background!=null)g.DrawImage(background,new Rectangle(-10+(int)parallaxX,-7+(int)parallaxY,ClientSize.Width+20,ClientSize.Height+14));else g.Clear(Color.FromArgb(4,9,6));
        using(Brush shade=new SolidBrush(Color.FromArgb(88,0,5,2)))g.FillRectangle(shade,ClientRectangle);
        DrawFigure(g);DrawCamcorderOverlay(g);if(!bootComplete)DrawBoot(g);if(launchQueued)DrawSignalLoss(g);
    }

    void DrawCamcorderOverlay(Graphics g)
    {
        using(Font small=new Font("Consolas",10f,FontStyle.Bold))using(Font rec=new Font("Consolas",11f,FontStyle.Bold))using(Pen line=new Pen(Color.FromArgb(135,209,221,207),1f))
        {
            g.DrawString("● REC",rec,Brushes.OrangeRed,31,76);g.DrawString("SP",small,Brushes.Gainsboro,107,78);g.DrawString(DateTime.Now.ToString("HH:mm:ss"),small,Brushes.Gainsboro,145,78);g.DrawString("NV  AUTO",small,Brushes.Gainsboro,ClientSize.Width-133,78);
            g.DrawRectangle(line,ClientSize.Width-175,33,112,17);using(Brush battery=new SolidBrush(Color.FromArgb(165,202,231,205)))g.FillRectangle(battery,ClientSize.Width-171,37,82,9);g.DrawRectangle(line,ClientSize.Width-61,38,4,7);
            int edge=27,arm=24;g.DrawLine(line,edge,112,edge+arm,112);g.DrawLine(line,edge,112,edge,112+arm);g.DrawLine(line,ClientSize.Width-edge,112,ClientSize.Width-edge-arm,112);g.DrawLine(line,ClientSize.Width-edge,112,ClientSize.Width-edge,112+arm);
            g.DrawLine(line,edge,ClientSize.Height-27,edge+arm,ClientSize.Height-27);g.DrawLine(line,edge,ClientSize.Height-27,edge,ClientSize.Height-27-arm);g.DrawLine(line,ClientSize.Width-edge,ClientSize.Height-27,ClientSize.Width-edge-arm,ClientSize.Height-27);g.DrawLine(line,ClientSize.Width-edge,ClientSize.Height-27,ClientSize.Width-edge,ClientSize.Height-27-arm);
        }
        int scan=(Environment.TickCount/17)%Math.Max(1,ClientSize.Height);using(Pen p=new Pen(Color.FromArgb(24,188,222,190)))g.DrawLine(p,0,scan,ClientSize.Width,scan);
        if(!reduceFlashesValue)using(Brush b=new SolidBrush(Color.FromArgb(28,190,217,196)))for(int i=0;i<30;++i)g.FillRectangle(b,random.Next(ClientSize.Width),random.Next(ClientSize.Height),random.Next(1,4),1);
    }

    void DrawFigure(Graphics g)
    {
        if(reduceFlashesValue||DateTime.UtcNow>=figureUntil)return; double remaining=(figureUntil-DateTime.UtcNow).TotalMilliseconds;int alpha=(int)Math.Max(0,Math.Min(24,remaining/45));int x=596,y=285;
        using(Brush shadow=new SolidBrush(Color.FromArgb(alpha,0,0,0))){g.FillEllipse(shadow,x,y,38,49);Point[] body={new Point(x-28,y+105),new Point(x-12,y+48),new Point(x+19,y+39),new Point(x+50,y+50),new Point(x+71,y+116)};g.FillPolygon(shadow,body);}
        using(Brush eye=new SolidBrush(Color.FromArgb(alpha/2,210,225,205))){g.FillEllipse(eye,x+10,y+19,3,2);g.FillEllipse(eye,x+25,y+19,3,2);}
    }

    void DrawBoot(Graphics g)
    {
        double ms=(DateTime.UtcNow-bootStarted).TotalMilliseconds;
        if(reduceFlashesValue)
        {
            int calmAlpha=(int)Math.Max(0,245-(ms/3300.0)*245);
            using(Brush calm=new SolidBrush(Color.FromArgb(calmAlpha,0,3,1)))g.FillRectangle(calm,ClientRectangle);
            using(Font calmFont=new Font("Consolas",12f,FontStyle.Bold))using(Brush calmText=new SolidBrush(Color.FromArgb(205,183,225,190)))
                g.DrawString(ms<1800?"CAMERA POWERING ON":"SIGNAL READY",calmFont,calmText,46,665);
            return;
        }
        if(ms<430)
        {
            g.FillRectangle(Brushes.Black,ClientRectangle);
            int width=(int)(ClientSize.Width*Math.Max(0,ms-90)/340.0);
            using(Brush line=new SolidBrush(Color.FromArgb(210,196,227,202)))g.FillRectangle(line,(ClientSize.Width-width)/2,ClientSize.Height/2,width,2);
            return;
        }
        int coverAlpha=ms<920?235:(ms<1650?190:(ms<2450?125:(int)Math.Max(0,100-(ms-2450)/8.5)));
        using(Brush cover=new SolidBrush(Color.FromArgb(coverAlpha,0,4,2)))g.FillRectangle(cover,ClientRectangle);
        int trackingY=(Environment.TickCount/3)%ClientSize.Height;
        using(Brush tracking=new SolidBrush(Color.FromArgb(ms<1000?115:42,205,226,209)))g.FillRectangle(tracking,0,trackingY,ClientSize.Width,ms<1000?7:2);
        int noiseCount=ms<1050?460:(ms<1700?170:45);
        using(Brush noise=new SolidBrush(Color.FromArgb(ms<1050?92:38,205,229,210)))
            for(int i=0;i<noiseCount;++i)g.FillRectangle(noise,random.Next(ClientSize.Width),random.Next(ClientSize.Height),random.Next(2,22),random.Next(1,3));
        using(Font mono=new Font("Consolas",11f,FontStyle.Bold))using(Font large=new Font("Consolas",17f,FontStyle.Bold))using(Brush green=new SolidBrush(Color.FromArgb(220,179,231,191)))
        {
            g.DrawString("MURKOFF DIGITAL VIDEO CAMERA",large,green,42,585);
            if(ms>720)g.DrawString("POWER ............ ON",mono,green,45,622);
            if(ms>1080)g.DrawString("TAPE ............. SP / 00:00:00",mono,green,45,647);
            if(ms>1480)g.DrawString("AUTO EXPOSURE .... SETTLING",mono,green,45,672);
            if(ms>1950)g.DrawString("OPENXR SIGNAL .... ACQUIRED",mono,green,45,697);
            if(ms>2520)g.DrawString("● REC READY",large,Brushes.OrangeRed,45,722);
        }
        if(ms>950&&ms<1550)
        {
            int roll=(int)((ms-950)/600.0*ClientSize.Height);
            using(Brush rollBrush=new LinearGradientBrush(new Rectangle(0,Math.Max(0,roll-90),ClientSize.Width,180),Color.FromArgb(0,190,225,198),Color.FromArgb(80,190,225,198),90f))
                g.FillRectangle(rollBrush,0,Math.Max(0,roll-90),ClientSize.Width,180);
        }
    }

    void DrawSignalLoss(Graphics g)
    {
        using(Brush black=new SolidBrush(Color.FromArgb(165,0,0,0)))g.FillRectangle(black,ClientRectangle);
        using(Font font=new Font("Consolas",18f,FontStyle.Bold))using(Brush text=new SolidBrush(Color.FromArgb(218,200,224,204)))g.DrawString(reduceFlashesValue?"STARTING OPENXR SESSION":"SIGNAL LOST // SWITCHING TO HEADSET",font,text,330,350);
        if(!reduceFlashesValue)using(Brush bar=new SolidBrush(Color.FromArgb(85,185,222,192)))for(int i=0;i<14;++i)g.FillRectangle(bar,0,random.Next(ClientSize.Height),ClientSize.Width,random.Next(1,8));
    }

    void QueueLaunch(){if(launchQueued)return;SavePreferences();launchQueued=true;launchAt=DateTime.UtcNow.AddMilliseconds(reduceFlashesValue?420:920);play.Enabled=false;StopMusic();}

    void LaunchGameCore()
    {
        try
        {
            if(previewMode){statusLine.Text="PREVIEW: LAUNCH TRANSITION COMPLETE";play.Enabled=true;return;}
            ProcessStartInfo psi=new ProcessStartInfo();psi.FileName=exePath;psi.WorkingDirectory=gameDir;psi.UseShellExecute=false;
            if(steamOpenXR.Checked){string steam=FindSteamVRRuntime();if(steam==null)throw new InvalidOperationException("SteamVR OpenXR was not found. Choose Default OpenXR or install SteamVR.");psi.EnvironmentVariables["XR_RUNTIME_JSON"]=steam;}
            else if(psi.EnvironmentVariables.ContainsKey("XR_RUNTIME_JSON"))psi.EnvironmentVariables.Remove("XR_RUNTIME_JSON");
            if(Process.GetProcessesByName("Outlast2").Length!=0)throw new InvalidOperationException("Close Outlast 2 before starting a new VR session.");
            WritePrivateProfileString("VR","GameplaySrgb",colorSrgb.Checked?"1":"0",Path.Combine(gameDir,"outlast2_vr_p35.ini"));
            string settings=VrSettings.PathForUser(),backup="not changed (current game graphics selected)",hudSettings=VrSettings.HudPathForSettings(settings),hudBackup="not changed";
            if(graphicsSafe.Checked){backup=VrSettings.Apply(settings)??"unchanged; already VR-safe";hudBackup=VrSettings.ApplyHud(hudSettings)??"unchanged; VR interaction settings already applied";}
            psi.EnvironmentVariables["OUTLAST2VR_SETTINGS_PATH"]=settings;
            File.AppendAllText(Path.Combine(gameDir,"Outlast2VR_launcher.log"),DateTime.UtcNow.ToString("o")+" "+BuildName+"\r\nRuntime: "+(steamOpenXR.Checked?"SteamVR":FriendlyRuntime(GetDefaultRuntime()))+"\r\nColor: "+(colorSrgb.Checked?"sRGB":"legacy UNORM")+"\r\nGraphics: "+(graphicsSafe.Checked?"VR-safe":"keep current")+"\r\nSettings: "+settings+"\r\nBackup: "+backup+"\r\nHUD settings: "+hudSettings+"\r\nGameplay/HUD backup: "+hudBackup+"\r\n");
            Process.Start(psi);Close();
        }
        catch(Exception ex){play.Enabled=true;MessageBox.Show(ex.Message,"Outlast 2 VR - Launch failed",MessageBoxButtons.OK,MessageBoxIcon.Error);StartMusic();}
    }

    void RefreshDiagnostics()
    {
        if(diagnosticsText==null)return;string runtime=GetDefaultRuntime();bool game=File.Exists(exePath),mod=File.Exists(Path.Combine(gameDir,"dinput8.dll")),loader=File.Exists(Path.Combine(gameDir,"openxr_loader.dll")),ini=File.Exists(Path.Combine(gameDir,"outlast2_vr_p35.ini"));
        StringBuilder t=new StringBuilder();t.AppendLine(Status(game,"GAME",game?"Outlast2.exe found":"Outlast2.exe missing"));t.AppendLine(Status(mod,"VR MOD",mod?"dinput8.dll found":"dinput8.dll missing"));t.AppendLine(Status(loader,"OPENXR",loader?"loader found":"openxr_loader.dll missing"));t.AppendLine(Status(ini,"CONFIG",ini?"Beta 1 PF17 configuration found":"outlast2_vr_p35.ini missing"));t.AppendLine();t.AppendLine(Status(!String.IsNullOrEmpty(runtime),"RUNTIME",FriendlyRuntime(runtime)));t.AppendLine("  ACTIVE JSON    "+(String.IsNullOrEmpty(runtime)?"Not configured":runtime));t.AppendLine("  SERVICE        "+RuntimeServiceStatus(runtime));t.AppendLine("  HEADSET        Verified when game OpenXR session starts");t.AppendLine("  CONTROLLERS    Verified when game action sets attach");t.AppendLine();t.AppendLine("  ACTIVE BUILD   "+BuildName);t.AppendLine("  GAMEPLAY CORE  PF17 native view + progression; hybrid tracked arms");diagnosticsText.Text=t.ToString();if(mainRuntime!=null)mainRuntime.Text="OPENXR  //  "+FriendlyRuntime(runtime);
    }
    static string Status(bool good,string name,string value){return(good?"[ OK ] ":"[ !! ] ")+name.PadRight(10)+value;}
    static string RuntimeServiceStatus(string runtime){try{string lower=(runtime??"").ToLowerInvariant();string[] names=lower.Contains("virtualdesktop")?new string[]{"VirtualDesktop.Streamer","VirtualDesktop.Server"}:lower.Contains("steam")?new string[]{"vrserver","vrmonitor"}:new string[]{"OVRServer_x64","OculusClient","vrserver","VirtualDesktop.Streamer"};foreach(string n in names)if(Process.GetProcessesByName(n).Length>0)return"Runtime process detected (session not yet opened)";}catch{}return"Not detected yet; start headset/runtime before Play VR";}

    void LoadPreferences(){Prefs.ReduceFlashes=ReadPreference("ReduceFlashes",false);Prefs.UseSteamVr=ReadPreference("UseSteamVr",false);Prefs.UseSrgb=ReadPreference("UseSrgb",true);Prefs.SafeGraphics=ReadPreference("SafeGraphics",true);}
    void ApplyPreferencesToControls(){defaultOpenXR.Checked=!Prefs.UseSteamVr;steamOpenXR.Checked=Prefs.UseSteamVr;colorSrgb.Checked=Prefs.UseSrgb;colorLegacy.Checked=!Prefs.UseSrgb;graphicsSafe.Checked=Prefs.SafeGraphics;graphicsKeep.Checked=!Prefs.SafeGraphics;reduceFlashes.Checked=Prefs.ReduceFlashes;}
    void SavePreferences()
    {
        if(defaultOpenXR==null)return;Prefs.UseSteamVr=steamOpenXR.Checked;Prefs.UseSrgb=colorSrgb.Checked;Prefs.SafeGraphics=graphicsSafe.Checked;Prefs.ReduceFlashes=reduceFlashes.Checked;
        try{StringBuilder b=new StringBuilder();b.AppendLine("[Launcher]");b.AppendLine("UseSteamVr="+(Prefs.UseSteamVr?"1":"0"));b.AppendLine("UseSrgb="+(Prefs.UseSrgb?"1":"0"));b.AppendLine("SafeGraphics="+(Prefs.SafeGraphics?"1":"0"));b.AppendLine("ReduceFlashes="+(Prefs.ReduceFlashes?"1":"0"));string temp=preferencesPath+".tmp";File.WriteAllText(temp,b.ToString());if(File.Exists(preferencesPath))File.Replace(temp,preferencesPath,null);else File.Move(temp,preferencesPath);}catch{}
    }
    bool ReadPreference(string name,bool fallback){try{if(!File.Exists(preferencesPath))return fallback;foreach(string line in File.ReadAllLines(preferencesPath)){int equal=line.IndexOf('=');if(equal>0&&line.Substring(0,equal).Trim().Equals(name,StringComparison.OrdinalIgnoreCase))return line.Substring(equal+1).Trim()=="1";}}catch{}return fallback;}

    void LoadBackground(){try{using(Stream s=Assembly.GetExecutingAssembly().GetManifestResourceStream("Outlast2VR.launcher.background.png"))if(s!=null)using(Image image=Image.FromStream(s))background=new Bitmap(image);}catch{}}
    void StartBootSound(){if(reduceFlashesValue)return;try{bootWave=MakeStaticWave();bootSound=new SoundPlayer(bootWave);bootSound.Play();}catch{}}
    MemoryStream MakeStaticWave()
    {
        const int rate=11025,samples=8800;MemoryStream s=new MemoryStream();BinaryWriter w=new BinaryWriter(s);w.Write(Encoding.ASCII.GetBytes("RIFF"));w.Write(36+samples*2);w.Write(Encoding.ASCII.GetBytes("WAVEfmt "));w.Write(16);w.Write((short)1);w.Write((short)1);w.Write(rate);w.Write(rate*2);w.Write((short)2);w.Write((short)16);w.Write(Encoding.ASCII.GetBytes("data"));w.Write(samples*2);double phase=0;
        for(int i=0;i<samples;++i){phase+=2*Math.PI*(i<1200?84:46)/rate;double fade=Math.Min(1.0,Math.Min(i/320.0,(samples-i)/900.0));double noise=(random.NextDouble()*2-1)*.12+Math.Sin(phase)*.05;w.Write((short)(noise*fade*32767));}w.Flush();s.Position=0;return s;
    }
    string FindMenuMusic(){string[] names={"Outlast2VR_menu_music.mp3","Outlast2VR_menu_music.wav","Outlast2VR_menu_music.wma"};foreach(string n in names){string p=Path.Combine(gameDir,n);if(File.Exists(p))return p;}return null;}
    void StartMusic(){if(musicPlaying||String.IsNullOrEmpty(musicPath))return;StopMusic();string safe=musicPath.Replace("\"","");int result=mciSendString("open \""+safe+"\" alias "+MusicAlias,null,0,IntPtr.Zero);if(result==0){mciSendString("setaudio "+MusicAlias+" volume to 300",null,0,IntPtr.Zero);result=mciSendString("play "+MusicAlias+" repeat",null,0,IntPtr.Zero);}musicPlaying=result==0;UpdateMusicButton();}
    void StopMusic(){mciSendString("stop "+MusicAlias,null,0,IntPtr.Zero);mciSendString("close "+MusicAlias,null,0,IntPtr.Zero);musicPlaying=false;if(musicButton!=null)UpdateMusicButton();}
    void ToggleMusic(){if(String.IsNullOrEmpty(musicPath)){MessageBox.Show("Place a legally obtained track beside Outlast2VR.exe named Outlast2VR_menu_music.mp3, .wav or .wma.","Optional music",MessageBoxButtons.OK,MessageBoxIcon.Information);return;}if(musicPlaying)StopMusic();else StartMusic();}
    void UpdateMusicButton(){if(musicButton!=null)musicButton.Text=String.IsNullOrEmpty(musicPath)?"MUSIC: ADD TRACK":(musicPlaying?"MUSIC: ON":"MUSIC: OFF");}
    void LauncherKeyDown(object sender,KeyEventArgs e){if(e.KeyCode==Keys.F1)ShowPage(LauncherPage.Controls);if(e.KeyCode==Keys.F2)ShowPage(LauncherPage.Diagnostics);if(e.KeyCode==Keys.F3)ShowPage(LauncherPage.Options);if(e.KeyCode==Keys.F11)SetFullscreen(!fullscreen);if(e.KeyCode==Keys.Escape&&fullscreen)SetFullscreen(false);}

    static Panel MakePanel(Point location,Size size){Panel p=new Panel();p.Location=location;p.Size=size;p.BackColor=Color.FromArgb(8,13,10);p.BorderStyle=BorderStyle.FixedSingle;return p;}
    static Panel MakeGroup(Point location,Size size){Panel p=new Panel();p.Location=location;p.Size=size;p.BackColor=Color.FromArgb(8,13,10);return p;}
    static Button MakeButton(string text,Point location,Size size,float fontSize){Button b=new Button();b.Text=text;b.Location=location;b.Size=size;b.Font=new Font("Segoe UI",fontSize,FontStyle.Bold);b.FlatStyle=FlatStyle.Flat;b.FlatAppearance.BorderColor=Color.FromArgb(89,104,90);b.BackColor=Color.FromArgb(11,17,13);b.ForeColor=Color.WhiteSmoke;b.Cursor=Cursors.Hand;return b;}
    static RadioButton MakeRadio(string text,Point location,Size size){RadioButton r=new RadioButton();r.Text=text;r.Location=location;r.Size=size;r.ForeColor=Color.WhiteSmoke;r.BackColor=Color.FromArgb(8,13,10);r.Font=new Font("Segoe UI",9.5f,FontStyle.Bold);return r;}
    static Label MakeLabel(string text,float size,FontStyle style,Color color,ContentAlignment align){Label l=new Label();l.Text=text;l.Font=new Font("Segoe UI",size,style);l.ForeColor=color;l.BackColor=Color.Transparent;l.TextAlign=align;return l;}
    static string GetDefaultRuntime(){string[] keys={@"SOFTWARE\Khronos\OpenXR\1",@"SOFTWARE\WOW6432Node\Khronos\OpenXR\1"};foreach(string n in keys)try{using(RegistryKey k=Registry.LocalMachine.OpenSubKey(n)){string v=k==null?null:k.GetValue("ActiveRuntime")as string;if(!String.IsNullOrWhiteSpace(v))return v;}}catch{}return null;}
    static string FriendlyRuntime(string path){if(String.IsNullOrWhiteSpace(path))return"NOT DETECTED";string l=path.ToLowerInvariant();if(l.Contains("virtualdesktop"))return"VIRTUAL DESKTOP OPENXR";if(l.Contains("steamxr")||l.Contains("steamvr"))return"STEAMVR OPENXR";if(l.Contains("oculus")||l.Contains("meta"))return"META / OCULUS OPENXR";return Path.GetFileName(path).ToUpperInvariant();}
    static string FindSteamVRRuntime(){string[] c={@"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json",@"C:\Program Files\Steam\steamapps\common\SteamVR\steamxr_win64.json"};try{using(RegistryKey k=Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")){string steam=k==null?null:k.GetValue("SteamPath")as string;if(!String.IsNullOrWhiteSpace(steam)){string p=Path.Combine(steam,@"steamapps\common\SteamVR\steamxr_win64.json");if(File.Exists(p))return p;}}}catch{}foreach(string p in c)if(File.Exists(p))return p;return null;}
    static class Prefs{public static bool UseSteamVr,ReduceFlashes;public static bool UseSrgb=true,SafeGraphics=true;}
}

public class ControllerDiagram : Control
{
    Image controllers;
    public ControllerDiagram()
    {
        DoubleBuffered=true;ForeColor=Color.WhiteSmoke;
        try{using(Stream s=Assembly.GetExecutingAssembly().GetManifestResourceStream("Outlast2VR.quest.controllers.png"))if(s!=null)using(Image image=Image.FromStream(s))controllers=new Bitmap(image);}catch{}
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);Graphics g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;g.InterpolationMode=InterpolationMode.HighQualityBicubic;
        if(controllers!=null)g.DrawImage(controllers,new Rectangle(205,42,562,316));
        using(Font head=new Font("Segoe UI",12f,FontStyle.Bold))using(Font text=new Font("Segoe UI",9f,FontStyle.Bold))using(Brush white=new SolidBrush(Color.FromArgb(224,231,222)))using(Brush orange=new SolidBrush(Color.FromArgb(255,116,55)))using(Pen call=new Pen(Color.FromArgb(190,217,93,57),1.4f))
        {
            g.DrawString("LEFT CONTROLLER",head,orange,23,8);g.DrawString("RIGHT CONTROLLER",head,orange,755,8);
            Callout(g,call,text,white,"LEFT STICK\nMove / objects after X",new Point(365,99),new Point(14,74),false);
            Callout(g,call,text,white,"L3 CLICK\nSprint",new Point(365,119),new Point(17,153),false);
            Callout(g,call,text,white,"X BUTTON\nInteract / pick up / use",new Point(420,122),new Point(12,239),false);
            Callout(g,call,text,white,"RIGHT STICK\nTurn / camcorder zoom",new Point(650,99),new Point(787,72),true);
            Callout(g,call,text,white,"R3 CLICK\nNight vision / Y+R3 mic",new Point(650,119),new Point(785,151),true);
            Callout(g,call,text,white,"Y BUTTON\nReload / hold for bandage",new Point(594,122),new Point(748,235),true);
            Callout(g,call,text,white,"RIGHT GRIP\nRaise / lower camcorder",new Point(626,231),new Point(666,337),true);
        }
    }
    static void Callout(Graphics g,Pen pen,Font font,Brush brush,string label,Point anchor,Point labelPoint,bool right){int elbow=right?labelPoint.X-10:labelPoint.X+150;g.DrawLine(pen,anchor.X,anchor.Y,elbow,anchor.Y);g.DrawLine(pen,elbow,anchor.Y,elbow,labelPoint.Y+10);g.DrawString(label,font,brush,labelPoint.X,labelPoint.Y);}
}
