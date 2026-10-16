using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text.RegularExpressions;

namespace MultiseatPrototype {
    [DataContract]
    internal sealed class SeatConfiguration {
        [DataMember(Name = "seat", Order = 0)] public int Number;
        [DataMember(Name = "monitor", Order = 1)] public string Monitor;
        [DataMember(Name = "keyboard", Order = 2)] public string Keyboard;
        [DataMember(Name = "mouse", Order = 3)] public string Mouse;
        [DataMember(Name = "audio", Order = 4)] public string Audio;
        internal string Get(DeviceKind kind) {
            switch (kind) {
            case DeviceKind.Monitor: return Monitor;
            case DeviceKind.Keyboard: return Keyboard;
            case DeviceKind.Mouse: return Mouse;
            default: return Audio;
            }
        }
        internal void Set(DeviceKind kind, string value) {
            switch (kind) {
            case DeviceKind.Monitor: Monitor = value; break;
            case DeviceKind.Keyboard: Keyboard = value; break;
            case DeviceKind.Mouse: Mouse = value; break;
            default: Audio = value; break;
            }
        }
    }

    [DataContract]
    internal sealed class SeatPlan {
        [DataMember(Name = "version", Order = 0)] public int Version = 1;
        [DataMember(Name = "seats", Order = 1)] public List<SeatConfiguration> Seats;
        internal static SeatPlan Empty() {
            return new SeatPlan { Seats = new List<SeatConfiguration> {
                new SeatConfiguration { Number = 1 }, new SeatConfiguration { Number = 2 } } };
        }
        internal static List<string> Validate(SeatPlan plan, HardwareCatalog catalog) {
            List<string> errors = new List<string>();
            if (plan == null || plan.Version != 1 || plan.Seats == null || plan.Seats.Count != 2 ||
                plan.Seats[0] == null || plan.Seats[1] == null ||
                plan.Seats[0].Number != 1 || plan.Seats[1].Number != 2) {
                errors.Add("A configuração precisa conter as duas estações desta versão.");
                return errors;
            }
            foreach (DeviceKind kind in Enum.GetValues(typeof(DeviceKind))) {
                string first = plan.Seats[0].Get(kind);
                string second = plan.Seats[1].Get(kind);
                if (!String.IsNullOrEmpty(first) && first == second)
                    errors.Add(HardwareCatalog.KindLabel(kind) + ": escolha um dispositivo diferente para cada estação.");
                foreach (SeatConfiguration seat in plan.Seats) {
                    string id = seat.Get(kind);
                    if (String.IsNullOrEmpty(id)) continue; // A partial plan is a valid local draft.
                    if (!Regex.IsMatch(id, "^" + kind.ToString().ToLowerInvariant() + ":[0-9a-f]{64}$"))
                        errors.Add("Estação " + seat.Number + ": identificador de dispositivo inválido.");
                    else if (catalog != null && catalog.Find(kind, id) == null)
                        errors.Add("Estação " + seat.Number + ": " + HardwareCatalog.KindLabel(kind).ToLowerInvariant() + " desconectado. Revise a seleção.");
                }
            }
            return errors;
        }
    }

    internal sealed class ConfigurationStore {
        private readonly string directory;
        internal readonly string FilePath;
        internal ConfigurationStore(string root) {
            directory = Path.GetFullPath(root);
            FilePath = Path.Combine(directory, "settings.json");
        }
        internal static ConfigurationStore Local() {
            return new ConfigurationStore(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MultiseatPrototype"));
        }
        private static void RejectRedirect(string path) {
            if ((File.Exists(path) || Directory.Exists(path)) &&
                (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("O local de configuração foi redirecionado; o arquivo não foi alterado.");
        }
        internal SeatPlan Load() {
            RejectRedirect(directory); RejectRedirect(FilePath);
            if (!File.Exists(FilePath)) return SeatPlan.Empty();
            if (new FileInfo(FilePath).Length > 65536) throw new InvalidDataException("Configuração local muito grande.");
            SeatPlan plan;
            using (FileStream file = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.Read))
                plan = (SeatPlan)new DataContractJsonSerializer(typeof(SeatPlan)).ReadObject(file);
            if (SeatPlan.Validate(plan, null).Count != 0) throw new InvalidDataException("Configuração local inválida.");
            return plan;
        }
        internal void Save(SeatPlan plan) {
            if (SeatPlan.Validate(plan, null).Count != 0) throw new InvalidDataException("Configuração inválida; nada foi salvo.");
            RejectRedirect(directory); RejectRedirect(FilePath);
            Directory.CreateDirectory(directory);
            string temporary = Path.Combine(directory, "settings-" + Guid.NewGuid().ToString("N") + ".tmp");
            try {
                using (FileStream file = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                    new DataContractJsonSerializer(typeof(SeatPlan)).WriteObject(file, plan);
                    file.Flush(true);
                }
                RejectRedirect(FilePath);
                if (File.Exists(FilePath)) File.Replace(temporary, FilePath, null);
                else File.Move(temporary, FilePath);
            } finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }
    }
}
