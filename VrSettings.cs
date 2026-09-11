using System;
using System.IO;
using System.Text;
using System.Collections.Generic;

public static class VrSettings
{
    // Do not replace the user's whole config, resolution, gamma, controls or saves.
    static readonly string[] Disabled = {
        "MotionBlur", "MotionBlurPause",
        "bAllowTemporalAA", "bAllowPostprocessFXAA", "AllowRadialBlur",
        "AllowChromaticAberration", "Distortion", "FilteredDistortion",
        "AllowImageReflections", "AllowImageReflectionShadowing",
        "AmbientOcclusion", "HighQualityAmbientOcclusion", "UseVsync", "OneFrameThreadLag"
    };
    // These gates are part of Outlast 2's custom camera/night-vision chain.
    // Disabling them was introduced as a visual diagnostic and is not a safe
    // anti-blur setting.  Motion blur and both temporal AA paths stay disabled.
    static readonly string[] Enabled = { "DepthOfField", "bAllowRedBarrelsDOF" };
    public static string Transform(string original)
    {
        string newline = original.Contains("\r\n") ? "\r\n" : "\n";
        var lines = new List<string>(original.Replace("\r\n", "\n").Split('\n'));
        var values = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        foreach (var key in Disabled) values.Add(key,"False");
        foreach (var key in Enabled) values.Add(key,"True");
        values.Add("MotionBlurSkinning","0");
        values.Add("ScreenPercentage","100.000000");
        // These are native UE3/Outlast settings already present in the game's
        // quality profiles. Extend high-detail visibility without changing the
        // proven VR projection, post-processing chain, or texture streamer.
        values.Add("DecalCullDistanceScale","4.000000");
        values.Add("SkeletalMeshLODBias","0");
        values.Add("ParticleLODBias","0");
        values.Add("DetailMode","2");
        values.Add("MaxDrawDistanceScale","4.000000");
        values.Add("LODDistanceFactorScale","0.125000");
        bool inside=false, found=false;
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var output = new List<string>();
        Action finish = delegate {
            if (!inside) return;
            foreach (var pair in values) if (!seen.Contains(pair.Key)) output.Add(pair.Key+"="+pair.Value);
            seen.Clear();
        };
        foreach (string line in lines)
        {
            string trim=line.Trim();
            if (trim.StartsWith("[") && trim.EndsWith("]"))
            {
                finish();inside=trim.Equals("[SystemSettings]",StringComparison.OrdinalIgnoreCase);
                found |= inside;output.Add(line);continue;
            }
            int equal=line.IndexOf('=');
            string value;
            if (inside && equal>=0 && values.TryGetValue(line.Substring(0,equal).Trim(),out value))
            {
                string key=line.Substring(0,equal).Trim();seen.Add(key);
                int comment=line.IndexOf(';',equal+1);
                output.Add(line.Substring(0,equal+1)+value+(comment>=0 ? " "+line.Substring(comment) : ""));
            }
            else output.Add(line);
        }
        finish();
        if (!found) throw new InvalidDataException("The game settings do not contain [SystemSettings]. No settings were changed.");
        return string.Join(newline,output.ToArray());
    }
    public static string TransformHud(string original)
    {
        string newline = original.Contains("\r\n") ? "\r\n" : "\n";
        var lines = new List<string>(original.Replace("\r\n", "\n").Split('\n'));
        bool insideHud=false, insideHero=false, foundHud=false, foundHero=false;
        var output = new List<string>();
        foreach (string line in lines)
        {
            string trim=line.Trim();
            if (trim.StartsWith("[") && trim.EndsWith("]"))
            {
                insideHud=trim.Equals("[OLGame.OLHUD]",StringComparison.OrdinalIgnoreCase);
                insideHero=trim.Equals("[OLGame.OLHero]",StringComparison.OrdinalIgnoreCase);
                foundHud |= insideHud;foundHero |= insideHero;output.Add(line);continue;
            }
            int equal=line.IndexOf('=');
            string key=equal>=0 ? line.Substring(0,equal).Trim() : "";
            string replacement=null;
            if (insideHud && key.Equals("bShowCrosshair",StringComparison.OrdinalIgnoreCase))
                replacement="false";
            else if (insideHero && key.Equals("PickupInteractRadius",StringComparison.OrdinalIgnoreCase))
                replacement="200.0";
            else if (insideHero && key.Equals("MinCosAngleForPickup",StringComparison.OrdinalIgnoreCase))
                replacement="0.70";
            if (replacement!=null)
            {
                int comment=line.IndexOf(';',equal+1);
                output.Add(line.Substring(0,equal+1)+replacement+(comment>=0 ? " "+line.Substring(comment) : ""));
            }
            else output.Add(line);
        }
        if (!foundHud || !foundHero) throw new InvalidDataException("The game config does not contain the required Outlast HUD and Hero sections. No interaction settings were changed.");
        return string.Join(newline,output.ToArray());
    }
    public static string PathForUser()
    {
        var documents = new List<string>();
        Action<string> add = delegate(string root) {
            if (String.IsNullOrWhiteSpace(root)) return;
            string full=Path.GetFullPath(root);
            foreach(string existing in documents)
                if(String.Equals(existing,full,StringComparison.OrdinalIgnoreCase))return;
            documents.Add(full);
        };
        add(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments));
        add(Path.Combine(Environment.GetEnvironmentVariable("OneDrive") ?? "","Documents"));
        add(Path.Combine(Environment.GetEnvironmentVariable("OneDriveConsumer") ?? "","Documents"));
        add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),@"OneDrive\Documents"));
        return ResolveSettingsPath(documents);
    }
    public static string ResolveSettingsPath(IEnumerable<string> documentRoots)
    {
        string fallback=null;
        foreach(string root in documentRoots)
        {
            if(String.IsNullOrWhiteSpace(root))continue;
            string candidate=Path.Combine(root,@"My Games\Outlast2\OLGame\Config\OLSystemSettings.ini");
            if(fallback==null)fallback=candidate;
            if(File.Exists(candidate))return candidate;
        }
        if(fallback==null)throw new InvalidDataException("No Windows Documents folder could be resolved.");
        return fallback;
    }
    public static string HudPathForSettings(string settingsPath)
    {
        if (String.IsNullOrWhiteSpace(settingsPath)) throw new ArgumentException("Settings path is required.","settingsPath");
        return Path.Combine(Path.GetDirectoryName(Path.GetFullPath(settingsPath)),"OLGame.ini");
    }
    public static string Apply(string path)
    {
        return ApplyFile(path,Transform,"before-visual-screen");
    }
    public static string ApplyHud(string path)
    {
        return ApplyFile(path,TransformHud,"before-vr-no-crosshair");
    }
    static string ApplyFile(string path,Func<string,string> transform,string backupLabel)
    {
        if (!File.Exists(path)) throw new FileNotFoundException("Run Outlast 2 in desktop mode once to create your settings, then use the VR launcher.",path);
        byte[] bytes=File.ReadAllBytes(path);
        // UE3 accepts native ANSI or BOM-marked UTF-16, not a UTF-8 BOM.
        // StreamReader.CurrentEncoding is BOM-emitting UTF8 even when its input
        // was plain ASCII; reusing it had hidden the first [SystemSettings].
        bool utf8Bom=bytes.Length>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf;
        Encoding encoding;int skip=0;
        if(bytes.Length>=4&&((bytes[0]==0xff&&bytes[1]==0xfe&&bytes[2]==0&&bytes[3]==0)||
            (bytes[0]==0&&bytes[1]==0&&bytes[2]==0xfe&&bytes[3]==0xff)))
            throw new InvalidDataException("UTF-32 game config is not supported; no settings were changed.");
        if(utf8Bom){encoding=new UTF8Encoding(false,true);skip=3;}
        else if(bytes.Length>=2&&bytes[0]==0xff&&bytes[1]==0xfe){encoding=new UnicodeEncoding(false,true,true);skip=2;}
        else if(bytes.Length>=2&&bytes[0]==0xfe&&bytes[1]==0xff){encoding=new UnicodeEncoding(true,true,true);skip=2;}
        else {encoding=Encoding.Default;if(encoding.CodePage==65001)encoding=new UTF8Encoding(false,true);}
        string original=encoding.GetString(bytes,skip,bytes.Length-skip);
        string updated=transform(original);
        if(updated==original&&!utf8Bom) return null;
        if(utf8Bom){
            bool ascii=true;foreach(char c in updated)if(c>127){ascii=false;break;}
            encoding=ascii?(Encoding)Encoding.ASCII:new UnicodeEncoding(false,true,true);
        }
        string unique=DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff")+"-"+Guid.NewGuid().ToString("N").Substring(0,8);
        string backup=path+"."+backupLabel+"-"+unique+".bak";
        string temporary=path+".vr-"+unique+".tmp";
        try
        {
            File.WriteAllText(temporary,updated,encoding);
            // Same-directory atomic replacement keeps an exact recoverable original.
            File.Replace(temporary,path,backup);
        }
        finally { if(File.Exists(temporary)) File.Delete(temporary); }
        return backup;
    }
}
