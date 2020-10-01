//===--- Compilation.h - Compilation Task Data Structure --------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// TODO: Document me
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DRIVER_COMPILATION_H
#define SWIFT_DRIVER_COMPILATION_H

#include "swift/Basic/ArrayRefView.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/LangOptions.h"
#include "swift/Basic/NullablePtr.h"
#include "swift/Basic/OutputFileMap.h"
#include "swift/Basic/Statistic.h"
#include "swift/Driver/Driver.h"
#include "swift/Driver/Job.h"
#include "swift/Driver/Util.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"

#include <memory>
#include <vector>

namespace llvm {
namespace opt {
  class InputArgList;
  class DerivedArgList;
}
}

namespace swift {
  class DiagnosticEngine;
  namespace sys {
    class TaskQueue;
  }

namespace driver {
  class Driver;
  class ToolChain;
  class OutputInfo;
  class PerformJobsState;

/// An enum providing different levels of output which should be produced
/// by a Compilation.
enum class OutputLevel {
  /// Indicates that normal output should be produced.
  Normal,

  /// Indicates that only jobs should be printed and not run. (-###)
  PrintJobs,

  /// Indicates that verbose output should be produced. (-v)
  Verbose,

  /// Indicates that parseable output should be produced.
  Parseable,
};

/// Indicates whether a temporary file should always be preserved if a part of
/// the compilation crashes.
enum class PreserveOnSignal : bool {
  No,
  Yes
};

using CommandSet = llvm::SmallPtrSet<const Job *, 16>;

class Compilation {
public:
  class IncrementalSchemeComparator {
    const bool EnableIncrementalBuildWhenConstructed;
    const bool &EnableIncrementalBuild;
    const bool EnableSourceRangeDependencies;

    /// If not empty, the path to use to log the comparison.
    const StringRef CompareIncrementalSchemesPath;

    const unsigned SwiftInputCount;

  public:
    std::string WhyIncrementalWasDisabled = "";

  private:
    DiagnosticEngine &Diags;

    CommandSet JobsWithoutRanges;
    CommandSet JobsWithRanges;

    unsigned CompileStagesWithoutRanges = 0;
    unsigned CompileStagesWithRanges = 0;

  public:
    IncrementalSchemeComparator(const bool &EnableIncrementalBuild,
                                bool EnableSourceRangeDependencies,
                                const StringRef CompareIncrementalSchemesPath,
                                unsigned SwiftInputCount,
                                DiagnosticEngine &Diags)
        : EnableIncrementalBuildWhenConstructed(EnableIncrementalBuild),
          EnableIncrementalBuild(EnableIncrementalBuild),
          EnableSourceRangeDependencies(EnableSourceRangeDependencies),
          CompareIncrementalSchemesPath(CompareIncrementalSchemesPath),
          SwiftInputCount(SwiftInputCount), Diags(Diags) {}

    /// Record scheduled jobs in support of the
    /// -compare-incremental-schemes[-path] options
    void update(const CommandSet &withoutRangeJobs,
                const CommandSet &withRangeJobs);

    /// Write the information for the -compare-incremental-schemes[-path]
    /// options
    void outputComparison() const;

  private:
    void outputComparison(llvm::raw_ostream &) const;
  };

public:
  /// The filelist threshold value to pass to ensure file lists are never used
  static const size_t NEVER_USE_FILELIST = SIZE_MAX;

private:
  /// The DiagnosticEngine to which this Compilation should emit diagnostics.
  DiagnosticEngine &Diags;

  /// The ToolChain this Compilation was built with, that it may reuse to build
  /// subsequent BatchJobs.
  const ToolChain &TheToolChain;

  /// The OutputInfo, which the Compilation stores a copy of upon
  /// construction, and which it may use to build subsequent batch
  /// jobs itself.
  OutputInfo TheOutputInfo;

  /// The OutputLevel at which this Compilation should generate output.
  OutputLevel Level;

  /// The OutputFileMap describing the Compilation's outputs, populated both by
  /// the user-provided output file map (if it exists) and inference rules that
  /// derive otherwise-unspecified output filenames from context.
  OutputFileMap DerivedOutputFileMap;

  /// The Actions which were used to build the Jobs.
  ///
  /// This is mostly only here for lifetime management.
  SmallVector<std::unique_ptr<const Action>, 32> Actions;

  /// The Jobs which will be performed by this compilation.
  SmallVector<std::unique_ptr<const Job>, 32> Jobs;

  /// The original (untranslated) input argument list.
  ///
  /// This is only here for lifetime management. Any inspection of
  /// command-line arguments should use #getArgs().
  std::unique_ptr<llvm::opt::InputArgList> RawInputArgs;

  /// The translated input arg list.
  std::unique_ptr<llvm::opt::DerivedArgList> TranslatedArgs;

  /// A list of input files and their associated types.
  InputFileList InputFilesWithTypes;

  /// When non-null, a temporary file containing all input .swift files.
  /// Used for large compilations to avoid overflowing argv.
  const char *AllSourceFilesPath = nullptr;

  /// Temporary files that should be cleaned up after the compilation finishes.
  ///
  /// These apply whether the compilation succeeds or fails. If the
  llvm::StringMap<PreserveOnSignal> TempFilePaths;

  /// Write information about this compilation to this file.
  ///
  /// This is used for incremental builds.
  std::string CompilationRecordPath;

  /// A hash representing all the arguments that could trigger a full rebuild.
  std::string ArgsHash;

  /// When the build was started.
  ///
  /// This should be as close as possible to when the driver was invoked, since
  /// it's used as a lower bound.
  llvm::sys::TimePoint<> BuildStartTime;

  /// The time of the last build.
  ///
  /// If unknown, this will be some time in the past.
  llvm::sys::TimePoint<> LastBuildTime = llvm::sys::TimePoint<>::min();

  /// Indicates whether this Compilation should continue execution of subtasks
  /// even if they returned an error status.
  bool ContinueBuildingAfterErrors = false;

  /// Indicates whether tasks should only be executed if their output is out
  /// of date.
  bool EnableIncrementalBuild;

  /// Indicates whether groups of parallel frontend jobs should be merged
  /// together and run in composite "batch jobs" when possible, to reduce
  /// redundant work.
  const bool EnableBatchMode;

  /// Provides a randomization seed to batch-mode partitioning, for debugging.
  const unsigned BatchSeed;

  /// Overrides parallelism level and \c BatchSizeLimit, sets exact
  /// count of batches, if in batch-mode.
  const Optional<unsigned> BatchCount;

  /// Overrides maximum batch size, if in batch-mode and not overridden
  /// by \c BatchCount.
  const Optional<unsigned> BatchSizeLimit;

  /// True if temporary files should not be deleted.
  const bool SaveTemps;

  /// When true, dumps information on how long each compilation task took to
  /// execute.
  const bool ShowDriverTimeCompilation;

  /// When non-null, record various high-level counters to this.
  std::unique_ptr<UnifiedStatsReporter> Stats;

  /// When true, dumps information about why files are being scheduled to be
  /// rebuilt.
  bool ShowIncrementalBuildDecisions = false;

  /// When true, traces the lifecycle of each driver job. Provides finer
  /// detail than ShowIncrementalBuildDecisions.
  bool ShowJobLifecycle = false;

  /// When true, some frontend job has requested permission to pass
  /// -emit-loaded-module-trace, so no other job needs to do it.
  bool PassedEmitLoadedModuleTraceToFrontendJob = false;

  /// The limit for the number of files to pass on the command line. Beyond this
  /// limit filelists will be used.
  size_t FilelistThreshold;

  /// Because each frontend job outputs the same info in its .d file, only do it
  /// on the first job that actually runs. Write out dummies for the rest of the
  /// jobs. This hack saves a lot of time in the build system when incrementally
  /// building a project with many files. Record if a scheduled job has already
  /// added -emit-dependency-path.
  bool HaveAlreadyAddedDependencyPath = false;

public:
  /// When set, only the first scheduled frontend job gets the argument needed
  /// to produce a make-style dependency file. The other jobs create dummy files
  /// in the driver. This hack speeds up incremental compilation by reducing the
  /// time for the build system to read each dependency file, which are all
  /// identical. This optimization can be disabled by passing
  /// -disable-only-one-dependency-file on the command line.
  const bool OnlyOneDependencyFile;

private:
  /// Helpful for debugging, but slows down the driver. So, only turn on when
  /// needed.
  const bool VerifyFineGrainedDependencyGraphAfterEveryImport;
  /// Helpful for debugging, but slows down the driver. So, only turn on when
  /// needed.
  const bool EmitFineGrainedDependencyDotFileAfterEveryImport;

  /// Experiment with intrafile dependencies
  const bool FineGrainedDependenciesIncludeIntrafileOnes;

  /// Experiment with source-range-based dependencies
  const bool EnableSourceRangeDependencies;

  /// (experimental) Enable cross-module incremental build scheduling.
  const bool EnableCrossModuleIncrementalBuild;

public:
  /// Will contain a comparator if an argument demands it.
  Optional<IncrementalSchemeComparator> IncrementalComparator;

private:
  template <typename T>
  static T *unwrap(const std::unique_ptr<T> &p) {
    return p.get();
  }

  template <typename T>
  using UnwrappedArrayView =
      ArrayRefView<std::unique_ptr<T>, T *, Compilation::unwrap<T>>;

public:
  // clang-format off
  Compilation(DiagnosticEngine &Diags, const ToolChain &TC,
              OutputInfo const &OI,
              OutputLevel Level,
              std::unique_ptr<llvm::opt::InputArgList> InputArgs,
              std::unique_ptr<llvm::opt::DerivedArgList> TranslatedArgs,
              InputFileList InputsWithTypes,
              std::string CompilationRecordPath,
              StringRef ArgsHash, llvm::sys::TimePoint<> StartTime,
              llvm::sys::TimePoint<> LastBuildTime,
              size_t FilelistThreshold,
              bool EnableIncrementalBuild = false,
              bool EnableBatchMode = false,
              unsigned BatchSeed = 0,
              Optional<unsigned> BatchCount = None,
              Optional<unsigned> BatchSizeLimit = None,
              bool SaveTemps = false,
              bool ShowDriverTimeCompilation = false,
              std::unique_ptr<UnifiedStatsReporter> Stats = nullptr,
              bool OnlyOneDependencyFile = false,
              bool VerifyFineGrainedDependencyGraphAfterEveryImport = false,
              bool EmitFineGrainedDependencyDotFileAfterEveryImport = false,
              bool FineGrainedDependenciesIncludeIntrafileOnes = false,
              bool EnableSourceRangeDependencies = false,
              bool CompareIncrementalSchemes = false,
              StringRef CompareIncrementalSchemesPath = "",
              bool EnableCrossModuleIncrementalBuild = false);
  // clang-format on
  ~Compilation();

  ToolChain const &getToolChain() const {
    return TheToolChain;
  }

  OutputInfo const &getOutputInfo() const {
    return TheOutputInfo;
  }

  DiagnosticEngine &getDiags() const {
    return Diags;
  }

  UnwrappedArrayView<const Action> getActions() const {
    return llvm::makeArrayRef(Actions);
  }

  template <typename SpecificAction, typename... Args>
  SpecificAction *createAction(Args &&...args) {
    auto newAction = new SpecificAction(std::forward<Args>(args)...);
    Actions.emplace_back(newAction);
    return newAction;
  }

  UnwrappedArrayView<const Job> getJobs() const {
    return llvm::makeArrayRef(Jobs);
  }
  Job *addJob(std::unique_ptr<Job> J);

  /// To send job list to places that don't truck in fancy array views.
  std::vector<const Job *> getJobsSimply() const {
    return std::vector<const Job *>(getJobs().begin(), getJobs().end());
  }

  void addTemporaryFile(StringRef file,
                        PreserveOnSignal preserve = PreserveOnSignal::No) {
    TempFilePaths[file] = preserve;
  }

  bool isTemporaryFile(StringRef file) {
    return TempFilePaths.count(file);
  }

  const llvm::opt::DerivedArgList &getArgs() const { return *TranslatedArgs; }
  ArrayRef<InputPair> getInputFiles() const { return InputFilesWithTypes; }

  OutputFileMap &getDerivedOutputFileMap() { return DerivedOutputFileMap; }
  const OutputFileMap &getDerivedOutputFileMap() const {
    return DerivedOutputFileMap;
  }

  bool getIncrementalBuildEnabled() const {
    return EnableIncrementalBuild;
  }
  void disableIncrementalBuild(Twine why);

  bool getVerifyFineGrainedDependencyGraphAfterEveryImport() const {
    return VerifyFineGrainedDependencyGraphAfterEveryImport;
  }

  bool getEmitFineGrainedDependencyDotFileAfterEveryImport() const {
    return EmitFineGrainedDependencyDotFileAfterEveryImport;
  }

  bool getFineGrainedDependenciesIncludeIntrafileOnes() const {
    return FineGrainedDependenciesIncludeIntrafileOnes;
  }

  bool getEnableSourceRangeDependencies() const {
    return EnableSourceRangeDependencies;
  }

  bool getBatchModeEnabled() const {
    return EnableBatchMode;
  }

  bool getContinueBuildingAfterErrors() const {
    return ContinueBuildingAfterErrors;
  }
  void setContinueBuildingAfterErrors(bool Value = true) {
    ContinueBuildingAfterErrors = Value;
  }

  bool getShowIncrementalBuildDecisions() const {
    return ShowIncrementalBuildDecisions;
  }
  void setShowIncrementalBuildDecisions(bool value = true) {
    ShowIncrementalBuildDecisions = value;
  }

  bool getShowJobLifecycle() const {
    return ShowJobLifecycle;
  }
  void setShowJobLifecycle(bool value = true) {
    ShowJobLifecycle = value;
  }

  bool getShowDriverTimeCompilation() const {
    return ShowDriverTimeCompilation;
  }

  bool getEnableCrossModuleIncrementalBuild() const {
    return EnableCrossModuleIncrementalBuild;
  }

  size_t getFilelistThreshold() const {
    return FilelistThreshold;
  }

  /// Since every make-style dependency file contains
  /// the same information, incremental builds are sped up by only emitting one
  /// of those files. Since the build system expects to see the files existing,
  /// create dummy files for those jobs that don't emit real dependencies.
  /// \param path The dependency file path
  /// \param addDependencyPath A function to add an -emit-dependency-path
  /// argument
  void addDependencyPathOrCreateDummy(StringRef path,
                                      function_ref<void()> addDependencyPath);

  UnifiedStatsReporter *getStatsReporter() const {
    return Stats.get();
  }

  /// True if extra work has to be done when tracing through the dependency
  /// graph, either in order to print dependencies or to collect statistics.
  bool getTraceDependencies() const {
    return getShowIncrementalBuildDecisions() || getStatsReporter();
  }

  OutputLevel getOutputLevel() const {
    return Level;
  }

  unsigned getBatchSeed() const {
    return BatchSeed;
  }

  llvm::sys::TimePoint<> getLastBuildTime() const {
    return LastBuildTime;
  }

  Optional<unsigned> getBatchCount() const {
    return BatchCount;
  }

  Optional<unsigned> getBatchSizeLimit() const {
    return BatchSizeLimit;
  }

  /// Requests the path to a file containing all input source files. This can
  /// be shared across jobs.
  ///
  /// If this is never called, the Compilation does not bother generating such
  /// a file.
  ///
  /// \sa types::isPartOfSwiftCompilation
  const char *getAllSourcesPath() const;

  /// Asks the Compilation to perform the Jobs which it knows about.
  ///
  /// \param TQ The TaskQueue used to schedule jobs for execution.
  ///
  /// \returns result code for the Compilation's Jobs; 0 indicates success and
  /// -2 indicates that one of the Compilation's Jobs crashed during execution
  int performJobs(std::unique_ptr<sys::TaskQueue> &&TQ);

  /// Returns whether the callee is permitted to pass -emit-loaded-module-trace
  /// to a frontend job.
  ///
  /// This only returns true once, because only one job should pass that
  /// argument.
  bool requestPermissionForFrontendToEmitLoadedModuleTrace() {
    if (PassedEmitLoadedModuleTraceToFrontendJob)
      // Someone else has already done it!
      return false;
    else {
      // We're the first and only (to execute this path).
      PassedEmitLoadedModuleTraceToFrontendJob = true;
      return true;
    }
  }

  /// How many .swift input files?
  unsigned countSwiftInputs() const;

  /// Unfortunately the success or failure of a Swift compilation is currently
  /// sensitive to the order in which files are processed, at least in terms of
  /// the order of processing extensions (and likely other ways we haven't
  /// discovered yet). So long as this is true, we need to make sure any batch
  /// job we build names its inputs in an order that's a subsequence of the
  /// sequence of inputs the driver was initially invoked with.
  ///
  /// Also use to write out information in a consistent order.
  template <typename JobCollection>
  void sortJobsToMatchCompilationInputs(
      const JobCollection &unsortedJobs,
      SmallVectorImpl<const Job *> &sortedJobs) const;

private:
  /// Perform all jobs.
  ///
  /// \param[out] abnormalExit Set to true if any job exits abnormally (i.e.
  /// crashes).
  /// \param TQ The task queue on which jobs will be scheduled.
  ///
  /// \returns exit code of the first failed Job, or 0 on success. If a Job
  /// crashes during execution, a negative value will be returned.
  int performJobsImpl(bool &abnormalExit, std::unique_ptr<sys::TaskQueue> &&TQ);

  /// Performs a single Job by executing in place, if possible.
  ///
  /// \param Cmd the Job which should be performed.
  ///
  /// \returns Typically, this function will not return, as the current process
  /// will no longer exist, or it will call exit() if the program was
  /// successfully executed. In the event of an error, this function will return
  /// a negative value indicating a failure to execute.
  int performSingleCommand(const Job *Cmd);
};

} // end namespace driver
} // end namespace swift

#endif
