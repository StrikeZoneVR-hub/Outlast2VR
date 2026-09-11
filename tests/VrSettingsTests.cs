using System;
using System.IO;
using System.Text;
static class VrSettingsTests {
 static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
 static int Main(string[] args){
  if(args.Length==1 && args[0]=="--apply-user-settings"){
   string p=VrSettings.PathForUser();Console.WriteLine("Settings: "+p);Console.WriteLine("Backup: "+(VrSettings.Apply(p)??"already safe"));return 0;
  }
  string input="; keep me\r\n[SystemSettings]\r\nMotionBlur=True\r\nMotionBlur=True ; duplicate\r\nbAllowTemporalAA=True\r\nDepthOfField=False\r\nbAllowRedBarrelsDOF=False\r\nResX=1920\r\nGamma=1.7\r\nBloom=True\r\n[Other]\r\nMotionBlur=True\r\n";
  string output=VrSettings.Transform(input);
  Check(output.Contains("MotionBlur=False ; duplicate"),"duplicate flag override");
  Check(output.Contains("DepthOfField=True")&&output.Contains("bAllowRedBarrelsDOF=True"),"native night-vision postprocess gates restored");
  Check(output.Contains("MaxDrawDistanceScale=4.000000")&&
        output.Contains("LODDistanceFactorScale=0.125000")&&
        output.Contains("DecalCullDistanceScale=4.000000")&&
        output.Contains("DetailMode=2"),"native high-detail distance settings reduce object and decal pop-in");
  Check(output.Contains("[Other]\r\nMotionBlur=True"),"other sections preserved");
  Check(output.Contains("ResX=1920")&&output.Contains("Gamma=1.7")&&output.Contains("Bloom=True"),"unrelated values preserved");
  Check(VrSettings.Transform(output)==output,"idempotence");
  string hudInput="; keep hud\r\n[OLGame.OLHUD]\r\nbShowCrosshair=true\r\nbShowSubtitles=true\r\n[OLGame.OLHero]\r\nMinCosAngleForPickup=0.98\r\nPickupInteractRadius=30.0\r\nHealthRegenRate=5.0\r\n[Other]\r\nbShowCrosshair=true\r\n";
  string hudOutput=VrSettings.TransformHud(hudInput);
  Check(hudOutput.Contains("[OLGame.OLHUD]\r\nbShowCrosshair=false\r\nbShowSubtitles=true"),"native center-dot crosshair disabled");
  Check(hudOutput.Contains("MinCosAngleForPickup=0.70\r\nPickupInteractRadius=200.0"),"native standing pickup reach and VR aim cone applied");
  Check(hudOutput.Contains("HealthRegenRate=5.0"),"unrelated hero settings preserved");
  Check(hudOutput.Contains("[Other]\r\nbShowCrosshair=true"),"unrelated crosshair keys preserved");
  Check(VrSettings.TransformHud(hudOutput)==hudOutput,"HUD transform idempotence");
  bool rejected=false;try{VrSettings.Transform("[Other]\nKey=Value");}catch(InvalidDataException){rejected=true;}Check(rejected,"reject absent section");
  string dir=Path.Combine(Path.GetTempPath(),"Outlast2VR-settings-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
  string missingDocuments=Path.Combine(dir,"local-documents"),redirectedDocuments=Path.Combine(dir,"onedrive-documents");
  string redirectedSettings=Path.Combine(redirectedDocuments,@"My Games\Outlast2\OLGame\Config\OLSystemSettings.ini");
  Directory.CreateDirectory(Path.GetDirectoryName(redirectedSettings));File.WriteAllText(redirectedSettings,input);
  Check(VrSettings.ResolveSettingsPath(new string[]{missingDocuments,redirectedDocuments})==redirectedSettings,"redirected Documents settings discovery");
  string path=Path.Combine(dir,"settings.ini");File.WriteAllText(path,input);
  string backup=VrSettings.Apply(path);Check(File.ReadAllText(backup)==input,"backup exact");Check(File.ReadAllText(path)==output,"replacement exact");
  Check(File.ReadAllBytes(path)[0]==(byte)';',"must not add UTF-8 BOM to native ANSI config");
  Check(VrSettings.Apply(path)==null,"no unnecessary backup");
  string hudPath=Path.Combine(dir,"OLGame.ini");File.WriteAllText(hudPath,hudInput);
  string hudBackup=VrSettings.ApplyHud(hudPath);
  Check(File.ReadAllText(hudBackup)==hudInput,"HUD backup exact");
  Check(File.ReadAllText(hudPath)==hudOutput,"HUD replacement exact");
  Check(VrSettings.ApplyHud(hudPath)==null,"no unnecessary HUD backup");
  File.Delete(hudBackup);File.Delete(hudPath);
  File.Delete(backup);
  string sectionFirst=output.Substring(output.IndexOf("[SystemSettings]"));
  File.WriteAllText(path,sectionFirst,new UTF8Encoding(true));
  backup=VrSettings.Apply(path);
  Check(backup!=null,"repair bad encoding even when all flag values already match");
  Check(File.ReadAllBytes(path)[0]==(byte)'[',"engine must see section header at byte zero");
  Check(File.ReadAllBytes(backup)[0]==0xef,"broken original preserved byte-for-byte");
  Check(File.ReadAllText(path)==sectionFirst,"BOM repair preserves values");
  Check(VrSettings.Apply(path)==null,"BOM repair idempotent");File.Delete(backup);
  foreach(var encoding in new Encoding[]{new UnicodeEncoding(false,true),new UnicodeEncoding(true,true),Encoding.GetEncoding(1252)}){
   string encodedInput=input+"; caf\u00e9\r\n";
   File.WriteAllText(path,encodedInput,encoding);byte[] before=File.ReadAllBytes(path);
   backup=VrSettings.Apply(path);byte[] after=File.ReadAllBytes(path);
   Check(BitConverter.ToString(before)==BitConverter.ToString(File.ReadAllBytes(backup)),"exact encoding backup");
   Check(encoding.GetString(after).TrimStart('\ufeff')==VrSettings.Transform(encodedInput),"native encoding and non-ASCII text preserved");
   Check(VrSettings.Apply(path)==null,"native encoding idempotence");File.Delete(backup);
  }
  File.Delete(path);Directory.Delete(dir,true);
  Console.WriteLine("PASS: VR settings, byte-level encoding, BOM repair, UTF-16/ANSI preservation, backups, idempotence");return 0;
 }
}
