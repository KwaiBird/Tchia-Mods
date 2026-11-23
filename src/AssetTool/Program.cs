using UAssetAPI;
using UAssetAPI.UnrealTypes;
using UAssetAPI.ExportTypes;
using UAssetAPI.Kismet.Bytecode;

if (args.Length != 3) throw new ArgumentException("dump|batch|write|offsets <input> <output>");
if (args[0] == "write")
{
    var asset = UAsset.DeserializeJson(File.ReadAllText(args[1]));
    asset.Write(args[2]);
    var check = new UAsset(args[2], EngineVersion.VER_UE4_26);
    if (!check.VerifyBinaryEquality()) throw new InvalidDataException("Output does not round-trip");
}
else if (args[0] == "batch")
{
    int failures = 0;
    foreach (var line in File.ReadLines(args[1]))
    {
        var pair = line.Split('\t');
        try { Dump(pair[0], pair[1]); }
        catch (Exception ex) { Console.Error.WriteLine(pair[0] + ": " + ex.Message); failures++; }
    }
    if (failures != 0) Environment.Exit(1);
}
else if (args[0] == "offsets")
{
    var asset = new UAsset(args[1], EngineVersion.VER_UE4_26);
    var result = new Dictionary<string, List<uint>>();
    foreach (var function in asset.Exports.OfType<FunctionExport>())
    {
        uint offset = 0;
        var positions = new List<uint>();
        foreach (var expression in function.ScriptBytecode)
        {
            positions.Add(offset);
            expression.Visit(asset, ref offset, (_, _) => {});
        }
        result.Add(function.ObjectName.ToString(), positions);
    }
    File.WriteAllText(args[2], System.Text.Json.JsonSerializer.Serialize(result));
}
else if (args[0] == "dump") Dump(args[1], args[2]);
else throw new ArgumentException("Unknown command");

static void Dump(string input, string output)
{
    var asset = new UAsset(input, EngineVersion.VER_UE4_26);
    if (!asset.VerifyBinaryEquality()) throw new InvalidDataException("Asset does not round-trip exactly");
    File.WriteAllText(output, asset.SerializeJson(true));
    Console.WriteLine(Path.GetFileName(output));
}
