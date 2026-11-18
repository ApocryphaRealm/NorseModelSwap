// NorseModelSwap - swap meshes and materials by rule (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood), 2026-11-18.
// Two layers, both from the same Swap lines:
//  - data: armour and clothing items hold their meshes and material in the item assets (ItemAsset_Armor/Clothing
//    Male/Female, ItemAsset_ArmorBase Material); a rule with no When swaps the reference there, so every unit wearing
//    the item gets the new look. Applied at each map load.
//  - live: the mesh components of spawned actors (units, and the weapon actors their weapons spawn as). A component
//    showing the old mesh gets the new one (SetSkeletalMeshAsset / SetSkinnedAssetAndUpdate / SetStaticMesh), a
//    material slot holding the old material gets the new one (SetMaterial). A When condition (NorseConditions) limits
//    it to some units. Checked 1 s and 4 s after the actor begins play (Norse dresses a unit after it spawns).
// Nothing in the game's files changes. Modelled on Model Swapper (Skyrim). Plan: ASSESSMENT-AND-PLAN.md.
//
// models\*.ini (NorseManifest order), one [section] per rule; its name is only a label:
//   [Outcasts wear a different helmet]
//   Swap = /Game/.../SK_Helmet_A.SK_Helmet_A -> /Game/.../SK_Helmet_B.SK_Helmet_B   ; a mesh or a material, repeatable
//   When = IsEnemy AND UnitType(Outcast_*)        ; optional; only on these units (live layer only)
//   Actors = BP_Metahuman_Unit_C                  ; whose components (class names, wildcards); default the units
//   Chance = 100                                  ; per actor
// Console: mdl.list [class] (components, meshes and materials of the first actors), mdl.apply, mdl.reload.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseFilters.hpp"

#include <deque>
#include <set>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    std::wstring PathOfObj(UObject* o)
    {
        std::wstring full = o->GetFullName(nullptr);
        return full.substr(full.find(L' ') + 1);
    }

    struct Pair
    {
        std::wstring from, to;
        UObject* toObj = nullptr;
        int32_t toIdx = -1;
        bool Ready() const { return toObj && Alive(toObj, toIdx); }
    };
    struct Rule
    {
        std::string name, where;
        std::vector<Pair> swaps;
        std::wstring when;
        Filter actors;
        int cond = 0, chance = 100;
        bool condTried = false;
    };
    std::vector<Rule> g_rules;

    using NcCompileFn = int (*)(const char*, char*, int);
    using NcEvalFn = int (*)(int, void*, void*);
    NcCompileFn g_ncCompile = nullptr;
    NcEvalFn g_ncEval = nullptr;

    template <typename F>
    F ModExport(const wchar_t* mod, const char* name)
    {
        HMODULE m = GetModuleHandleW((g_mod.parent_path() / mod / L"dlls" / L"main.dll").c_str());
        return m ? reinterpret_cast<F>(GetProcAddress(m, name)) : nullptr;
    }

    void CompileConditions()
    {
        if (!g_ncCompile) g_ncCompile = ModExport<NcCompileFn>(L"NorseConditions", "NC_Compile");
        if (!g_ncEval) g_ncEval = ModExport<NcEvalFn>(L"NorseConditions", "NC_Eval");
        for (Rule& r : g_rules)
        {
            if (r.when.empty() || r.cond || r.condTried) continue;
            if (!g_ncCompile)
            {
                NRN_WARN("[%s] When = %s, but NorseConditions is not loaded - the rule is off until it is", r.name.c_str(), Narrow(r.when).c_str());
                continue;
            }
            char err[256]{};
            r.condTried = true;
            r.cond = g_ncCompile(Narrow(r.when).c_str(), err, sizeof(err));
            if (!r.cond) NRN_ERROR("[%s] When does not compile: %s - the rule is off", r.name.c_str(), err);
        }
    }

    void LoadRules()
    {
        g_rules.clear();
        fs::path dir = g_mod / "models";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        for (const auto& f : files)
        {
            Rule* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_rules.push_back({});
                    cur = &g_rules.back();
                    cur->name = Narrow(Trim(l.substr(1, l.size() - 2)));
                    cur->where = where;
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not a [rule] or key = value line - skipped", where.c_str());
                    continue;
                }
                std::wstring k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
                if (k == L"swap")
                {
                    size_t arrow = v.find(L"->");
                    if (arrow == std::wstring::npos) NRN_WARN("%s: Swap needs 'old -> new' - skipped", where.c_str());
                    else cur->swaps.push_back({Trim(v.substr(0, arrow)), Trim(v.substr(arrow + 2))});
                }
                else if (k == L"when") cur->when = v;
                else if (k == L"actors") cur->actors = ParseFilter(v);
                else if (k == L"chance") cur->chance = ParseChance(v);
                else NRN_WARN("%s: unknown key '%s' (Swap, When, Actors, Chance)", where.c_str(), Narrow(k).c_str());
            }
        }
        size_t pairs = 0;
        for (Rule& r : g_rules)
        {
            if (r.actors.Empty()) r.actors.inc.push_back(L"bp_metahuman_unit_c");
            pairs += r.swaps.size();
        }
        CompileConditions();
        NRN_INFO("model rules: %zu rule(s), %zu swap(s) from %zu file(s)", g_rules.size(), pairs, files.size());
    }

    // Load each rule's new assets (kept loaded: the item asset or component holding them may not keep them alive)
    void ResolveTargets()
    {
        for (Rule& r : g_rules)
            for (Pair& p : r.swaps)
            {
                if (p.toObj && !p.Ready())
                {
                    NRN_WARN("[%s] %s was freed since it was loaded - loading it again", r.name.c_str(), Narrow(ShortName(p.to)).c_str());
                    p.toObj = nullptr;
                }
                if (p.toObj) continue;
                p.toObj = LoadKept(p.to.c_str());
                if (!p.toObj)
                {
                    NRN_WARN("[%s] %s: not found - this swap is skipped", r.name.c_str(), Narrow(p.to).c_str());
                    continue;
                }
                p.toIdx = IndexOf(p.toObj);
                bool rooted = false;
                try
                {
                    rooted = p.toObj->IsRootSet();
                }
                catch (...)
                {
                }
                NRN_INFO("[%s] %s loaded (index %d, root set %s)", r.name.c_str(), Narrow(ShortName(p.to)).c_str(), p.toIdx, rooted ? "yes" : "NO");
            }
    }

    // ------------------------------------------------------------------ data layer
    std::wstring ReplaceAllI(const std::wstring& s, const std::wstring& from, const std::wstring& to, int& count)
    {
        std::wstring ls = Lower(s), lf = Lower(from), out;
        size_t pos = 0;
        for (size_t hit; (hit = ls.find(lf, pos)) != std::wstring::npos; pos = hit + from.size())
        {
            out += s.substr(pos, hit - pos) + to;
            ++count;
        }
        return out + s.substr(pos);
    }

    void ApplyData(const char* why)
    {
        std::vector<const Pair*> global;
        for (const Rule& r : g_rules)
            if (r.when.empty() && r.chance >= 100)
                for (const Pair& p : r.swaps)
                    if (p.Ready()) global.push_back(&p);
        if (global.empty()) return;
        std::vector<UObject*> objs;
        try
        {
            RC::Unreal::UObjectGlobals::FindAllOf(L"ItemAsset", objs);
        }
        catch (...)
        {
        }
        int fields = 0, refs = 0;
        for (UObject* o : objs)
        {
            auto* cls = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(o));
            ForEachProp(cls, [&](FProperty* p) {
                void* addr = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
                std::wstring text = Export(p, addr, o);
                if (text == L"<unreadable>" || text.size() < 8) return;
                int hits = 0;
                std::wstring now = text;
                for (const Pair* s : global) now = ReplaceAllI(now, s->from, s->to, hits);
                if (!hits) return;
                bool faulted = false;
                if (!SehImport(p, now.c_str(), addr, o, &faulted) || faulted)
                {
                    NRN_WARN("%s.%s: Unreal did not accept the swapped value", Narrow(ShortName(PathOfObj(o))).c_str(), Narrow(p->GetName()).c_str());
                    return;
                }
                ++fields;
                RecordWrite(o, p, addr, now, ShortName(PathOfObj(o)) + L"." + std::wstring(p->GetName()));
                refs += hits;
                NRN_INFO("data: %d reference(s) swapped in %s.%s", hits, Narrow(ShortName(PathOfObj(o))).c_str(), Narrow(p->GetName()).c_str());
            });
        }
        NRN_INFO("data swaps (%s): %d reference(s) in %d field(s) of %zu item(s)", why, refs, fields, objs.size());
    }

    // ------------------------------------------------------------------ live layer
    std::wstring ClassName(UObject* o)
    {
        UClass* c = Reflect::Get().ClassOf(o);
        return c ? ShortName(PathOfObj(reinterpret_cast<UObject*>(c))) : L"";
    }
    bool IsA(UObject* o, const wchar_t* cls)
    {
        for (UStruct* c = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(o)); c; c = c->GetInheritanceSuper())
            if (_wcsicmp(ShortName(PathOfObj(reinterpret_cast<UObject*>(c))).c_str(), cls) == 0) return true;
        return false;
    }
    bool ActorPasses(const Filter& f, const std::wstring& cls)
    {
        std::wstring l = Lower(cls);
        if (!f.inc.empty() && std::none_of(f.inc.begin(), f.inc.end(), [&](auto& p) { return Glob(p.c_str(), l.c_str()); })) return false;
        return std::none_of(f.exc.begin(), f.exc.end(), [&](auto& p) { return Glob(p.c_str(), l.c_str()); });
    }

    std::vector<UObject*> MeshComponents(UObject* actor)
    {
        std::vector<UObject*> out;
        static UObject* meshCls = FindObj(L"/Script/Engine.MeshComponent");
        if (!meshCls || Reflect::Get().ParmsSizeOf(actor, L"K2_GetComponentsByClass") != 24) return out;
        Buf b;
        std::memcpy(b.b, &meshCls, 8);
        if (!Reflect::Get().Call(actor, L"K2_GetComponentsByClass", b.b, 24, sizeof(b.b))) return out;
        UObject** data = nullptr;
        int32_t num = 0;
        std::memcpy(&data, b.b + 8, 8);
        std::memcpy(&num, b.b + 16, 4);
        if (data && num > 0 && num < 512) out.assign(data, data + num);
        if (data) RC::Unreal::FMemory::Free(data); // the returned array is ours to free
        return out;
    }

    UObject* ObjField(UObject* o, const wchar_t* name)
    {
        FProperty* p = FindProp(reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(o)), name);
        UObject* v = nullptr;
        if (p) std::memcpy(&v, reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal(), 8);
        return v;
    }
    UObject* MeshOf(UObject* comp, bool& skinned)
    {
        skinned = IsA(comp, L"SkinnedMeshComponent");
        if (skinned)
        {
            UObject* m = ObjField(comp, L"SkinnedAsset");
            return m ? m : ObjField(comp, L"SkeletalMesh");
        }
        return IsA(comp, L"StaticMeshComponent") ? ObjField(comp, L"StaticMesh") : nullptr;
    }
    bool SetMesh(UObject* comp, bool skinned, UObject* mesh)
    {
        Buf b;
        std::memcpy(b.b, &mesh, 8);
        if (skinned)
        {
            if (Reflect::Get().ParmsSizeOf(comp, L"SetSkeletalMeshAsset") == 8 && IsA(comp, L"SkeletalMeshComponent"))
                return Reflect::Get().Call(comp, L"SetSkeletalMeshAsset", b.b, 8, sizeof(b.b));
            return Reflect::Get().Call(comp, L"SetSkinnedAssetAndUpdate", b.b, 9, sizeof(b.b)); // bReinitPose = false
        }
        return Reflect::Get().Call(comp, L"SetStaticMesh", b.b, 9, sizeof(b.b));
    }
    int NumMaterials(UObject* comp)
    {
        Buf b;
        if (!Reflect::Get().Call(comp, L"GetNumMaterials", b.b, 4, sizeof(b.b))) return 0;
        int32_t n = 0;
        std::memcpy(&n, b.b, 4);
        return std::clamp(n, 0, 64);
    }
    UObject* MaterialAt(UObject* comp, int i)
    {
        Buf b;
        int32_t idx = i;
        std::memcpy(b.b, &idx, 4);
        if (!Reflect::Get().Call(comp, L"GetMaterial", b.b, 16, sizeof(b.b))) return nullptr;
        UObject* m = nullptr;
        std::memcpy(&m, b.b + 8, 8);
        return m;
    }
    bool SetMaterialAt(UObject* comp, int i, UObject* mat)
    {
        Buf b;
        int32_t idx = i;
        std::memcpy(b.b, &idx, 4);
        std::memcpy(b.b + 8, &mat, 8);
        return Reflect::Get().Call(comp, L"SetMaterial", b.b, 16, sizeof(b.b));
    }
    bool Same(UObject* o, const std::wstring& path)
    {
        return o && _wcsicmp(PathOfObj(o).c_str(), path.c_str()) == 0;
    }

    UObject* Subject(UObject* actor)
    {
        if (IsA(actor, L"BP_Metahuman_Unit_C")) return actor;
        Buf b;
        if (Reflect::Get().ParmsSizeOf(actor, L"GetOwner") == 8 && Reflect::Get().Call(actor, L"GetOwner", b.b, 8, sizeof(b.b)))
        {
            UObject* o = nullptr;
            std::memcpy(&o, b.b, 8);
            if (o) return o;
        }
        return actor;
    }

    int SwapActor(UObject* actor, const char* why)
    {
        std::wstring cls = ClassName(actor), name = ShortName(PathOfObj(actor));
        std::vector<const Rule*> rules;
        for (const Rule& r : g_rules)
        {
            if (!ActorPasses(r.actors, cls)) continue;
            if (!r.when.empty() && (!r.cond || !g_ncEval || !g_ncEval(r.cond, Subject(actor), nullptr))) continue;
            if (r.chance < 100 && static_cast<int>(Fnv(name + L"|" + Widen(r.name)) % 100) >= r.chance) continue;
            rules.push_back(&r);
        }
        if (rules.empty()) return 0;
        int changed = 0;
        for (UObject* comp : MeshComponents(actor))
        {
            if (!comp) continue;
            bool skinned = false;
            UObject* mesh = MeshOf(comp, skinned);
            std::string cname = Narrow(ShortName(PathOfObj(comp)));
            for (const Rule* r : rules)
                for (const Pair& p : r->swaps)
                {
                    if (!p.Ready()) continue;
                    if (Same(mesh, p.from))
                    {
                        bool ok = SetMesh(comp, skinned, p.toObj);
                        NRN_INFO("%s %s.%s: mesh %s -> %s%s (%s)", why, Narrow(name).c_str(), cname.c_str(), Narrow(ShortName(p.from)).c_str(),
                                 Narrow(ShortName(p.to)).c_str(), ok ? "" : " FAILED", r->name.c_str());
                        if (ok)
                        {
                            ++changed;
                            mesh = p.toObj;
                        }
                    }
                    for (int i = 0, n = NumMaterials(comp); i < n; ++i)
                        if (Same(MaterialAt(comp, i), p.from))
                        {
                            bool ok = SetMaterialAt(comp, i, p.toObj);
                            NRN_INFO("%s %s.%s: material %d %s -> %s%s (%s)", why, Narrow(name).c_str(), cname.c_str(), i, Narrow(ShortName(p.from)).c_str(),
                                     Narrow(ShortName(p.to)).c_str(), ok ? "" : " FAILED", r->name.c_str());
                            if (ok) ++changed;
                        }
                }
        }
        return changed;
    }

    // Actors waiting for their 1 s and 4 s checks (kept with their object index: one can be destroyed meanwhile)
    struct Pending
    {
        UObject* actor;
        int32_t index;
        ULONGLONG at;
        int pass;
    };
    std::deque<Pending> g_pending;

    bool Wanted(UObject* actor)
    {
        std::wstring cls = ClassName(actor);
        return std::any_of(g_rules.begin(), g_rules.end(), [&](const Rule& r) { return ActorPasses(r.actors, cls); });
    }

    void Tick()
    {
        ULONGLONG now = GetTickCount64();
        if (!g_pending.empty() && g_pending.front().at <= now) ResolveTargets();
        for (size_t n = g_pending.size(); n > 0 && !g_pending.empty() && g_pending.front().at <= now; --n)
        {
            Pending p = g_pending.front();
            g_pending.pop_front();
            if (!Alive(p.actor, p.index)) continue;
            try
            {
                SwapActor(p.actor, p.pass == 1 ? "spawn" : "spawn+4s");
            }
            catch (...)
            {
                NRN_ERROR("live swap threw");
            }
            if (p.pass == 1) g_pending.push_back({p.actor, p.index, now + 3000, 2});
        }
    }

    std::vector<UObject*> ActorsNamed(const std::vector<std::wstring>& classes)
    {
        std::vector<UObject*> out;
        for (const auto& c : classes)
        {
            if (c.find(L'*') != std::wstring::npos) continue;
            std::vector<UObject*> found;
            try
            {
                RC::Unreal::UObjectGlobals::FindAllOf(c.c_str(), found);
            }
            catch (...)
            {
            }
            for (UObject* o : found)
                if (ShortName(PathOfObj(o)).rfind(L"Default__", 0) != 0 && Alive(o, IndexOf(o))) out.push_back(o);
        }
        return out;
    }

    void ApplyLive(const char* why)
    {
        std::set<std::wstring> classes;
        for (const Rule& r : g_rules)
            for (const auto& c : r.actors.inc) classes.insert(c);
        int actors = 0, changed = 0;
        for (UObject* a : ActorsNamed({classes.begin(), classes.end()}))
        {
            ++actors;
            changed += SwapActor(a, why);
        }
        NRN_INFO("live swaps (%s): %d change(s) on %d actor(s)", why, changed, actors);
    }

    void List(const std::wstring& cls, int max)
    {
        int shown = 0;
        for (UObject* a : ActorsNamed({cls.empty() ? L"BP_Metahuman_Unit_C" : cls}))
        {
            if (shown++ >= max) break;
            NRN_INFO("actor %s (%s)", Narrow(ShortName(PathOfObj(a))).c_str(), Narrow(ClassName(a)).c_str());
            for (UObject* comp : MeshComponents(a))
            {
                bool skinned = false;
                UObject* mesh = MeshOf(comp, skinned);
                std::string mats;
                for (int i = 0, n = NumMaterials(comp); i < n; ++i)
                {
                    UObject* m = MaterialAt(comp, i);
                    mats += (i ? ", " : "") + (m ? Narrow(PathOfObj(m)) : std::string("-"));
                }
                NRN_INFO("  %s (%s): mesh %s | materials %s", Narrow(ShortName(PathOfObj(comp))).c_str(), Narrow(ClassName(comp)).c_str(),
                         mesh ? Narrow(PathOfObj(mesh)).c_str() : "-", mats.c_str());
            }
        }
        NRN_INFO("mdl.list: %d actor(s) of %s", std::min(shown, max), Narrow(cls.empty() ? L"BP_Metahuman_Unit_C" : cls).c_str());
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Trim(c), arg;
        if (size_t sp = cmd.find(L' '); sp != std::wstring::npos)
        {
            arg = Trim(cmd.substr(sp + 1));
            cmd = cmd.substr(0, sp);
        }
        cmd = Lower(cmd);
        NRN_INFO("command: %s %s", Narrow(cmd).c_str(), Narrow(arg).c_str());
        if (cmd == L"mdl.reload")
        {
            LoadRules();
            ResolveTargets();
            ApplyData("mdl.reload");
            ApplyLive("mdl.reload");
        }
        else if (cmd == L"mdl.apply")
        {
            ResolveTargets();
            ApplyData("mdl.apply");
            ApplyLive("mdl.apply");
        }
        else if (cmd == L"mdl.list") List(arg, 3);
        else NRN_WARN("unknown command - mdl.list [class] | mdl.apply | mdl.reload");
        return true;
    }
} // namespace

class NorseModelSwap : public RC::CppUserModBase
{
  public:
    NorseModelSwap()
    {
        ModName = L"NorseModelSwap";
        ModVersion = L"" NMS_VERSION;
        ModDescription = L"Swap meshes and materials by rule: in item data, and on spawned units and weapons.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        for (const auto& raw : ReadLines(g_mod / "NorseModelSwap.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorseModelSwap.log", 2);
        Log::Get().Write(Level::Error, "NorseModelSwap v%s", NMS_VERSION);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                g_pending.clear();
                CompileConditions();
                try
                {
                    ResolveTargets();
                    RecheckWrites("map load"); // before the pass: it exports first, and a freed pointer would fault
                    ApplyData("map load");
                }
                catch (...)
                {
                    NRN_ERROR("map-load data pass threw");
                }
            });
            RC::Unreal::Hook::RegisterBeginPlayPostCallback([](RC::Unreal::AActor* a) {
                auto* o = reinterpret_cast<UObject*>(a);
                if (g_rules.empty() || !o) return;
                try
                {
                    if (Wanted(o)) g_pending.push_back({o, IndexOf(o), GetTickCount64() + 1000, 1});
                }
                catch (...)
                {
                }
            });
            RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float) {
                if (!g_pending.empty()) Tick();
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"mdl.", 4) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseModelSwap();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
