import Foundation

struct LocalASRModel {
    let name: String
    let path: String
    let sha256: String
}
struct LocalASRProfile: Identifiable {
    let id: String
    let model: LocalASRModel
}

enum ModelCatalog {
    static var resourcesPath: String? { Bundle.main.resourceURL?.appendingPathComponent("ModelResources").path }
    struct Manifest: Decodable { let models: [Entry] }
    struct Entry: Decodable { let name: String; let type: String; let filename: String; let sha256: String }
    struct Profile: Decodable { let id: String; let filename: String; let sha256: String }
    struct Selection: Decodable { let filename: String; let sha256: String; let profiles: [Profile]? }
    static func bundledProfiles(resourceRoot: URL? = Bundle.main.resourceURL?.appendingPathComponent("ModelResources")) -> [LocalASRProfile] {
        guard let resources = resourceRoot,
              let data = try? Data(contentsOf: resources.appendingPathComponent("manifest.json")),
              let manifest = try? JSONDecoder().decode(Manifest.self, from: data),
              let selected = try? Data(contentsOf: resources.appendingPathComponent("selection.json")),
              let selection = try? JSONDecoder().decode(Selection.self, from: selected) else { return [] }
        let profiles = selection.profiles ?? [Profile(id: "Balanced", filename: selection.filename, sha256: selection.sha256)]
        return profiles.compactMap { profile in
            guard let entry = manifest.models.first(where: { $0.type == "asr" && $0.filename == profile.filename && $0.sha256 == profile.sha256 }) else { return nil }
            let path = resources.appendingPathComponent(entry.filename).resolvingSymlinksInPath().path
            guard path.hasPrefix(resources.resolvingSymlinksInPath().path + "/"), FileManager.default.fileExists(atPath: path) else { return nil }
            // The native model owner verifies all bytes before inference.
            return LocalASRProfile(id: profile.id, model: LocalASRModel(name: entry.name, path: path, sha256: entry.sha256))
        }
    }
    static func bundledASR() -> LocalASRModel? { bundledProfiles().first(where: { $0.id == "Balanced" })?.model }
}
