using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace Reflgen.VisualStudio
{
    // 생성을 돌릴 프로젝트 하나 — 활성 구성 기준.
    internal sealed record ProjectTarget(string ProjectFile, string DisplayName, string Configuration, string Platform,
                                         IVsHierarchy Hierarchy);

    // VS 프로젝트 시스템과의 경계. 모두 UI 스레드에서 부른다. reflgen.targets 가 정한 속성·항목 메타데이터를
    // IVsBuildPropertyStorage 로 읽고 쓴다(.vcxproj 가 MSBuild 로 평가한 값이다).
    internal sealed class ProjectBridge
    {
        private const string ImportedProperty = "ReflgenTargetsImported";
        private const string CatalogProperty = "ReflgenCatalogFile";
        private const string GenerateMetadata = "ReflgenGenerate";

        private readonly IServiceProvider _services;

        public ProjectBridge(IServiceProvider services)
        {
            _services = services;
        }

        public bool UsesReflgen(IVsHierarchy hierarchy)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            return string.Equals(GetProperty(hierarchy, ImportedProperty), "true", StringComparison.OrdinalIgnoreCase);
        }

        public bool IsRegistered(IVsHierarchy hierarchy, uint itemId)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            return hierarchy is IVsBuildPropertyStorage storage &&
                   storage.GetItemAttribute(itemId, GenerateMetadata, out string value) == VSConstants.S_OK &&
                   string.Equals(value, "true", StringComparison.OrdinalIgnoreCase);
        }

        // 항목에 ReflgenGenerate=true 를 달고 프로젝트 파일을 저장한다.
        public bool Register(IVsHierarchy hierarchy, uint itemId)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (hierarchy is not IVsBuildPropertyStorage storage ||
                storage.SetItemAttribute(itemId, GenerateMetadata, "true") != VSConstants.S_OK)
            {
                return false;
            }
            var solution = (IVsSolution)_services.GetService(typeof(SVsSolution));
            solution?.SaveSolutionElement((uint)__VSSLNSAVEOPTIONS.SLNSAVEOPT_SaveIfDirty, hierarchy, 0);
            return true;
        }

        public ProjectTarget? TargetOf(IVsHierarchy hierarchy)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (hierarchy.GetCanonicalName(VSConstants.VSITEMID_ROOT, out string projectFile) != VSConstants.S_OK ||
                !File.Exists(projectFile))
            {
                return null;
            }
            string configuration = ActiveConfigurationName(hierarchy) ?? string.Empty;
            string[] parts = configuration.Split('|');
            if (parts.Length != 2)
            {
                return null;
            }
            hierarchy.GetProperty(VSConstants.VSITEMID_ROOT, (int)__VSHPROPID.VSHPROPID_Name, out object name);
            return new ProjectTarget(projectFile, name as string ?? Path.GetFileNameWithoutExtension(projectFile),
                                     parts[0], parts[1], hierarchy);
        }

        public IEnumerable<string> CatalogFiles()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var files = new List<string>();
            foreach (IVsHierarchy project in LoadedProjects())
            {
                string? file = UsesReflgen(project) ? GetProperty(project, CatalogProperty) : null;
                if (!string.IsNullOrEmpty(file))
                {
                    files.Add(file!);
                }
            }
            return files;
        }

        public string? SolutionFile()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var solution = (IVsSolution)_services.GetService(typeof(SVsSolution));
            if (solution == null || solution.GetSolutionInfo(out _, out string file, out _) != VSConstants.S_OK)
            {
                return null;
            }
            return string.IsNullOrEmpty(file) ? null : file;
        }

        // VS 빌드가 도는 중이면 생성은 그 빌드가 한다.
        public bool IsBuildRunning()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var builds = (IVsSolutionBuildManager2)_services.GetService(typeof(SVsSolutionBuildManager));
            if (builds == null || builds.QueryBuildManagerBusy(out int busy) != VSConstants.S_OK)
            {
                return false;
            }
            return busy != 0;
        }

        // 이 VS 에 딸린 MSBuild. 64비트판을 먼저 쓴다.
        public string? BundledMSBuild()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var shell = (IVsShell)_services.GetService(typeof(SVsShell));
            if (shell == null ||
                shell.GetProperty((int)__VSSPROPID.VSSPROPID_InstallDirectory, out object directory) != VSConstants.S_OK)
            {
                return null;
            }
            // InstallDirectory 는 <VS>\Common7\IDE\ 다.
            string root = Path.GetFullPath(Path.Combine((string)directory, "..", ".."));
            foreach (string candidate in new[] { @"MSBuild\Current\Bin\amd64\MSBuild.exe", @"MSBuild\Current\Bin\MSBuild.exe" })
            {
                string path = Path.Combine(root, candidate);
                if (File.Exists(path))
                {
                    return path;
                }
            }
            return null;
        }

        private string? GetProperty(IVsHierarchy hierarchy, string name)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (hierarchy is not IVsBuildPropertyStorage storage)
            {
                return null;
            }
            // 출력 경로 같은 값은 구성마다 다르다 — 활성 구성으로 평가한다.
            return storage.GetPropertyValue(name, ActiveConfigurationName(hierarchy),
                                            (uint)_PersistStorageType.PST_PROJECT_FILE, out string value) == VSConstants.S_OK
                       ? value
                       : null;
        }

        // "Debug|x64"
        private string? ActiveConfigurationName(IVsHierarchy hierarchy)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var builds = (IVsSolutionBuildManager2)_services.GetService(typeof(SVsSolutionBuildManager));
            var configurations = new IVsProjectCfg[1];
            if (builds == null ||
                builds.FindActiveProjectCfg(IntPtr.Zero, IntPtr.Zero, hierarchy, configurations) != VSConstants.S_OK ||
                configurations[0] == null)
            {
                return null;
            }
            return configurations[0].get_CanonicalName(out string name) == VSConstants.S_OK ? name : null;
        }

        private IEnumerable<IVsHierarchy> LoadedProjects()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            var solution = (IVsSolution)_services.GetService(typeof(SVsSolution));
            var projects = new List<IVsHierarchy>();
            Guid any = Guid.Empty;
            if (solution == null ||
                solution.GetProjectEnum((uint)__VSENUMPROJFLAGS.EPF_LOADEDINSOLUTION, ref any, out IEnumHierarchies items) !=
                    VSConstants.S_OK)
            {
                return projects;
            }
            var batch = new IVsHierarchy[1];
            while (items.Next(1, batch, out uint fetched) == VSConstants.S_OK && fetched == 1)
            {
                projects.Add(batch[0]);
            }
            return projects;
        }
    }
}
