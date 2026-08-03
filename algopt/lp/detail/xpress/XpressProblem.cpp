// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algopt/lp/detail/xpress/XpressProblem.h"

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/XpressConstraint.h"
#include "algopt/lp/detail/xpress/XpressExpression.h"
#include "algopt/lp/detail/xpress/XpressRelation.h"
#include "algopt/lp/detail/xpress/XpressVariable.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/logging/xlog.h>

#include <filesystem>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#define REBALANCER_XPRS_PARAM(name) {#name, name}

namespace {

#if XPVERSION == 47

// Parameters from
// third-party/xpress/9.9.0/x86_64/include/xprs.h
const std::map<std::string_view, int> kIntParamCodes = {
    REBALANCER_XPRS_PARAM(XPRS_EXTRAROWS),
    REBALANCER_XPRS_PARAM(XPRS_EXTRACOLS),
    REBALANCER_XPRS_PARAM(XPRS_LPITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_LPLOG),
    REBALANCER_XPRS_PARAM(XPRS_SCALING),
    REBALANCER_XPRS_PARAM(XPRS_PRESOLVE),
    REBALANCER_XPRS_PARAM(XPRS_CRASH),
    REBALANCER_XPRS_PARAM(XPRS_PRICINGALG),
    REBALANCER_XPRS_PARAM(XPRS_INVERTFREQ),
    REBALANCER_XPRS_PARAM(XPRS_INVERTMIN),
    REBALANCER_XPRS_PARAM(XPRS_MAXNODE),
    REBALANCER_XPRS_PARAM(XPRS_MAXTIME),
    REBALANCER_XPRS_PARAM(XPRS_MAXMIPSOL),
    REBALANCER_XPRS_PARAM(XPRS_SIFTPASSES),
    REBALANCER_XPRS_PARAM(XPRS_DEFAULTALG),
    REBALANCER_XPRS_PARAM(XPRS_VARSELECTION),
    REBALANCER_XPRS_PARAM(XPRS_NODESELECTION),
    REBALANCER_XPRS_PARAM(XPRS_BACKTRACK),
    REBALANCER_XPRS_PARAM(XPRS_MIPLOG),
    REBALANCER_XPRS_PARAM(XPRS_KEEPNROWS),
    REBALANCER_XPRS_PARAM(XPRS_MPSECHO),
    REBALANCER_XPRS_PARAM(XPRS_MAXPAGELINES),
    REBALANCER_XPRS_PARAM(XPRS_OUTPUTLOG),
    REBALANCER_XPRS_PARAM(XPRS_BARSOLUTION),
    REBALANCER_XPRS_PARAM(XPRS_CACHESIZE),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVER),
    REBALANCER_XPRS_PARAM(XPRS_BARITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_CHOLESKYALG),
    REBALANCER_XPRS_PARAM(XPRS_BAROUTPUT),
    REBALANCER_XPRS_PARAM(XPRS_EXTRAMIPENTS),
    REBALANCER_XPRS_PARAM(XPRS_REFACTOR),
    REBALANCER_XPRS_PARAM(XPRS_BARTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_KEEPBASIS),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVEROPS),
    REBALANCER_XPRS_PARAM(XPRS_VERSION),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_BIGMMETHOD),
    REBALANCER_XPRS_PARAM(XPRS_MPSNAMELENGTH),
    REBALANCER_XPRS_PARAM(XPRS_ELIMFILLIN),
    REBALANCER_XPRS_PARAM(XPRS_PRESOLVEOPS),
    REBALANCER_XPRS_PARAM(XPRS_MIPPRESOLVE),
    REBALANCER_XPRS_PARAM(XPRS_MIPTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_BARORDER),
    REBALANCER_XPRS_PARAM(XPRS_BREADTHFIRST),
    REBALANCER_XPRS_PARAM(XPRS_AUTOPERTURB),
    REBALANCER_XPRS_PARAM(XPRS_DENSECOLLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_CALLBACKFROMMAINTHREAD),
    REBALANCER_XPRS_PARAM(XPRS_MAXMCOEFFBUFFERELEMS),
    REBALANCER_XPRS_PARAM(XPRS_REFINEOPS),
    REBALANCER_XPRS_PARAM(XPRS_LPREFINEITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_MIPREFINEITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_DUALIZEOPS),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_PREBASISRED),
    REBALANCER_XPRS_PARAM(XPRS_PRESORT),
    REBALANCER_XPRS_PARAM(XPRS_PREPERMUTE),
    REBALANCER_XPRS_PARAM(XPRS_PREPERMUTESEED),
    REBALANCER_XPRS_PARAM(XPRS_MAXMEMORYSOFT),
    REBALANCER_XPRS_PARAM(XPRS_CUTFREQ),
    REBALANCER_XPRS_PARAM(XPRS_SYMSELECT),
    REBALANCER_XPRS_PARAM(XPRS_SYMMETRY),
    REBALANCER_XPRS_PARAM(XPRS_MAXMEMORYHARD),
    REBALANCER_XPRS_PARAM(XPRS_MIQCPALG),
    REBALANCER_XPRS_PARAM(XPRS_QCCUTS),
    REBALANCER_XPRS_PARAM(XPRS_QCROOTALG),
    REBALANCER_XPRS_PARAM(XPRS_PRECONVERTSEPARABLE),
    REBALANCER_XPRS_PARAM(XPRS_ALGAFTERNETWORK),
    REBALANCER_XPRS_PARAM(XPRS_TRACE),
    REBALANCER_XPRS_PARAM(XPRS_MAXIIS),
    REBALANCER_XPRS_PARAM(XPRS_CPUTIME),
    REBALANCER_XPRS_PARAM(XPRS_COVERCUTS),
    REBALANCER_XPRS_PARAM(XPRS_GOMCUTS),
    REBALANCER_XPRS_PARAM(XPRS_LPFOLDING),
    REBALANCER_XPRS_PARAM(XPRS_MPSFORMAT),
    REBALANCER_XPRS_PARAM(XPRS_CUTSTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_CUTDEPTH),
    REBALANCER_XPRS_PARAM(XPRS_TREECOVERCUTS),
    REBALANCER_XPRS_PARAM(XPRS_TREEGOMCUTS),
    REBALANCER_XPRS_PARAM(XPRS_CUTSELECT),
    REBALANCER_XPRS_PARAM(XPRS_TREECUTSELECT),
    REBALANCER_XPRS_PARAM(XPRS_DUALIZE),
    REBALANCER_XPRS_PARAM(XPRS_DUALGRADIENT),
    REBALANCER_XPRS_PARAM(XPRS_SBITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_SBBEST),
    REBALANCER_XPRS_PARAM(XPRS_BARINDEFLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_HEURFREQ),
    REBALANCER_XPRS_PARAM(XPRS_HEURDEPTH),
    REBALANCER_XPRS_PARAM(XPRS_HEURMAXSOL),
    REBALANCER_XPRS_PARAM(XPRS_HEURNODES),
    REBALANCER_XPRS_PARAM(XPRS_LNPBEST),
    REBALANCER_XPRS_PARAM(XPRS_LNPITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_BRANCHCHOICE),
    REBALANCER_XPRS_PARAM(XPRS_BARREGULARIZE),
    REBALANCER_XPRS_PARAM(XPRS_SBSELECT),
    REBALANCER_XPRS_PARAM(XPRS_IISLOG),
    REBALANCER_XPRS_PARAM(XPRS_LOCALCHOICE),
    REBALANCER_XPRS_PARAM(XPRS_LOCALBACKTRACK),
    REBALANCER_XPRS_PARAM(XPRS_DUALSTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_L1CACHE),
    REBALANCER_XPRS_PARAM(XPRS_HEURDIVESTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_HEURSELECT),
    REBALANCER_XPRS_PARAM(XPRS_BARSTART),
    REBALANCER_XPRS_PARAM(XPRS_PRESOLVEPASSES),
    REBALANCER_XPRS_PARAM(XPRS_BARNUMSTABILITY),
    REBALANCER_XPRS_PARAM(XPRS_BARORDERTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_EXTRASETS),
    REBALANCER_XPRS_PARAM(XPRS_FEASIBILITYPUMP),
    REBALANCER_XPRS_PARAM(XPRS_PRECOEFELIM),
    REBALANCER_XPRS_PARAM(XPRS_PREDOMCOL),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHFREQ),
    REBALANCER_XPRS_PARAM(XPRS_HEURDIVESPEEDUP),
    REBALANCER_XPRS_PARAM(XPRS_SBESTIMATE),
    REBALANCER_XPRS_PARAM(XPRS_BARCORES),
    REBALANCER_XPRS_PARAM(XPRS_MAXCHECKSONMAXTIME),
    REBALANCER_XPRS_PARAM(XPRS_MAXCHECKSONMAXCUTTIME),
    REBALANCER_XPRS_PARAM(XPRS_HISTORYCOSTS),
    REBALANCER_XPRS_PARAM(XPRS_ALGAFTERCROSSOVER),
    REBALANCER_XPRS_PARAM(XPRS_MUTEXCALLBACKS),
    REBALANCER_XPRS_PARAM(XPRS_BARCRASH),
    REBALANCER_XPRS_PARAM(XPRS_HEURDIVESOFTROUNDING),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHROOTSELECT),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHTREESELECT),
    REBALANCER_XPRS_PARAM(XPRS_MPS18COMPATIBLE),
    REBALANCER_XPRS_PARAM(XPRS_ROOTPRESOLVE),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERDRP),
    REBALANCER_XPRS_PARAM(XPRS_FORCEOUTPUT),
    REBALANCER_XPRS_PARAM(XPRS_PRIMALOPS),
    REBALANCER_XPRS_PARAM(XPRS_DETERMINISTIC),
    REBALANCER_XPRS_PARAM(XPRS_PREPROBING),
    REBALANCER_XPRS_PARAM(XPRS_TREEMEMORYLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_TREECOMPRESSION),
    REBALANCER_XPRS_PARAM(XPRS_TREEDIAGNOSTICS),
    REBALANCER_XPRS_PARAM(XPRS_MAXTREEFILESIZE),
    REBALANCER_XPRS_PARAM(XPRS_PRECLIQUESTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_REPAIRINFEASMAXTIME),
    REBALANCER_XPRS_PARAM(XPRS_IFCHECKCONVEXITY),
    REBALANCER_XPRS_PARAM(XPRS_PRIMALUNSHIFT),
    REBALANCER_XPRS_PARAM(XPRS_REPAIRINDEFINITEQ),
    REBALANCER_XPRS_PARAM(XPRS_MIPRAMPUP),
    REBALANCER_XPRS_PARAM(XPRS_MAXLOCALBACKTRACK),
    REBALANCER_XPRS_PARAM(XPRS_USERSOLHEURISTIC),
    REBALANCER_XPRS_PARAM(XPRS_PRECONVERTOBJTOCONS),
    REBALANCER_XPRS_PARAM(XPRS_FORCEPARALLELDUAL),
    REBALANCER_XPRS_PARAM(XPRS_BACKTRACKTIE),
    REBALANCER_XPRS_PARAM(XPRS_BRANCHDISJ),
    REBALANCER_XPRS_PARAM(XPRS_MIPFRACREDUCE),
    REBALANCER_XPRS_PARAM(XPRS_CONCURRENTTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_MAXSCALEFACTOR),
    REBALANCER_XPRS_PARAM(XPRS_HEURTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_THREADS),
    REBALANCER_XPRS_PARAM(XPRS_HEURBEFORELP),
    REBALANCER_XPRS_PARAM(XPRS_PREDOMROW),
    REBALANCER_XPRS_PARAM(XPRS_BRANCHSTRUCTURAL),
    REBALANCER_XPRS_PARAM(XPRS_QUADRATICUNSHIFT),
    REBALANCER_XPRS_PARAM(XPRS_BARPRESOLVEOPS),
    REBALANCER_XPRS_PARAM(XPRS_QSIMPLEXOPS),
    REBALANCER_XPRS_PARAM(XPRS_MIPRESTART),
    REBALANCER_XPRS_PARAM(XPRS_CONFLICTCUTS),
    REBALANCER_XPRS_PARAM(XPRS_PREPROTECTDUAL),
    REBALANCER_XPRS_PARAM(XPRS_CORESPERCPU),
    REBALANCER_XPRS_PARAM(XPRS_RESOURCESTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_CLAMPING),
    REBALANCER_XPRS_PARAM(XPRS_SLEEPONTHREADWAIT),
    REBALANCER_XPRS_PARAM(XPRS_PREDUPROW),
    REBALANCER_XPRS_PARAM(XPRS_CPUPLATFORM),
    REBALANCER_XPRS_PARAM(XPRS_BARALG),
    REBALANCER_XPRS_PARAM(XPRS_SIFTING),
    REBALANCER_XPRS_PARAM(XPRS_BARKEEPLASTSOL),
    REBALANCER_XPRS_PARAM(XPRS_LPLOGSTYLE),
    REBALANCER_XPRS_PARAM(XPRS_RANDOMSEED),
    REBALANCER_XPRS_PARAM(XPRS_TREEQCCUTS),
    REBALANCER_XPRS_PARAM(XPRS_PRELINDEP),
    REBALANCER_XPRS_PARAM(XPRS_DUALTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_PREOBJCUTDETECT),
    REBALANCER_XPRS_PARAM(XPRS_PREBNDREDQUAD),
    REBALANCER_XPRS_PARAM(XPRS_PREBNDREDCONE),
    REBALANCER_XPRS_PARAM(XPRS_PRECOMPONENTS),
    REBALANCER_XPRS_PARAM(XPRS_MAXMIPTASKS),
    REBALANCER_XPRS_PARAM(XPRS_MIPTERMINATIONMETHOD),
    REBALANCER_XPRS_PARAM(XPRS_PRECONEDECOMP),
    REBALANCER_XPRS_PARAM(XPRS_HEURFORCESPECIALOBJ),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHROOTCUTFREQ),
    REBALANCER_XPRS_PARAM(XPRS_PREELIMQUAD),
    REBALANCER_XPRS_PARAM(XPRS_PREIMPLICATIONS),
    REBALANCER_XPRS_PARAM(XPRS_TUNERMODE),
    REBALANCER_XPRS_PARAM(XPRS_TUNERMETHOD),
    REBALANCER_XPRS_PARAM(XPRS_TUNERTARGET),
    REBALANCER_XPRS_PARAM(XPRS_TUNERTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_TUNERHISTORY),
    REBALANCER_XPRS_PARAM(XPRS_TUNERPERMUTE),
    REBALANCER_XPRS_PARAM(XPRS_TUNERVERBOSE),
    REBALANCER_XPRS_PARAM(XPRS_TUNEROUTPUT),
    REBALANCER_XPRS_PARAM(XPRS_PREANALYTICCENTER),
    REBALANCER_XPRS_PARAM(XPRS_NETCUTS),
    REBALANCER_XPRS_PARAM(XPRS_LPFLAGS),
    REBALANCER_XPRS_PARAM(XPRS_MIPKAPPAFREQ),
    REBALANCER_XPRS_PARAM(XPRS_OBJSCALEFACTOR),
    REBALANCER_XPRS_PARAM(XPRS_TREEFILELOGINTERVAL),
    REBALANCER_XPRS_PARAM(XPRS_IGNORECONTAINERCPULIMIT),
    REBALANCER_XPRS_PARAM(XPRS_IGNORECONTAINERMEMORYLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_MIPDUALREDUCTIONS),
    REBALANCER_XPRS_PARAM(XPRS_GENCONSDUALREDUCTIONS),
    REBALANCER_XPRS_PARAM(XPRS_PWLDUALREDUCTIONS),
    REBALANCER_XPRS_PARAM(XPRS_BARFAILITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_AUTOSCALING),
    REBALANCER_XPRS_PARAM(XPRS_GENCONSABSTRANSFORMATION),
    REBALANCER_XPRS_PARAM(XPRS_COMPUTEJOBPRIORITY),
    REBALANCER_XPRS_PARAM(XPRS_PREFOLDING),
    REBALANCER_XPRS_PARAM(XPRS_COMPUTE),
    REBALANCER_XPRS_PARAM(XPRS_NETSTALLLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_SERIALIZEPREINTSOL),
    REBALANCER_XPRS_PARAM(XPRS_NUMERICALEMPHASIS),
    REBALANCER_XPRS_PARAM(XPRS_PWLNONCONVEXTRANSFORMATION),
    REBALANCER_XPRS_PARAM(XPRS_MIPCOMPONENTS),
    REBALANCER_XPRS_PARAM(XPRS_MIPCONCURRENTNODES),
    REBALANCER_XPRS_PARAM(XPRS_MIPCONCURRENTSOLVES),
    REBALANCER_XPRS_PARAM(XPRS_OUTPUTCONTROLS),
    REBALANCER_XPRS_PARAM(XPRS_SIFTSWITCH),
    REBALANCER_XPRS_PARAM(XPRS_HEUREMPHASIS),
    REBALANCER_XPRS_PARAM(XPRS_BARREFITER),
    REBALANCER_XPRS_PARAM(XPRS_COMPUTELOG),
    REBALANCER_XPRS_PARAM(XPRS_SIFTPRESOLVEOPS),
    REBALANCER_XPRS_PARAM(XPRS_CHECKINPUTDATA),
    REBALANCER_XPRS_PARAM(XPRS_ESCAPENAMES),
    REBALANCER_XPRS_PARAM(XPRS_IOTIMEOUT),
    REBALANCER_XPRS_PARAM(XPRS_AUTOCUTTING),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALNUMINITNLPCUTS),
    REBALANCER_XPRS_PARAM(XPRS_CALLBACKCHECKTIMEDELAY),
    REBALANCER_XPRS_PARAM(XPRS_MULTIOBJOPS),
    REBALANCER_XPRS_PARAM(XPRS_MULTIOBJLOG),
    REBALANCER_XPRS_PARAM(XPRS_LPBACKGROUNDTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_BACKGROUNDMAXTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALLSHEURSTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALSPATIALBRANCHIFPREFERORIG),
    REBALANCER_XPRS_PARAM(XPRS_PRECONFIGURATION),
    REBALANCER_XPRS_PARAM(XPRS_FEASIBILITYJUMP),
    REBALANCER_XPRS_PARAM(XPRS_IISOPS),
    REBALANCER_XPRS_PARAM(XPRS_RLTCUTS),
    REBALANCER_XPRS_PARAM(XPRS_ALTERNATIVEREDCOSTS),
    REBALANCER_XPRS_PARAM(XPRS_HEURSHIFTPROP),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHCOPYCONTROLS),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALNLPCUTS),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALTREENLPCUTS),
    REBALANCER_XPRS_PARAM(XPRS_BARHGOPS),
    REBALANCER_XPRS_PARAM(XPRS_BARHGMAXRESTARTS),
    REBALANCER_XPRS_PARAM(XPRS_MCFCUTSTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_PREROOTTHREADS),
    REBALANCER_XPRS_PARAM(XPRS_BARITERATIVE),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALPRESOLVEOBBT),
    REBALANCER_XPRS_PARAM(XPRS_SDPCUTSTRATEGY),
    REBALANCER_XPRS_PARAM(XPRS_DETERMINISTICLOG),
    REBALANCER_XPRS_PARAM(XPRS_BARHGGPU),
    REBALANCER_XPRS_PARAM(XPRS_BARHGPRECISION),
    REBALANCER_XPRS_PARAM(XPRS_BARHGGPUBLOCKSIZE),
    REBALANCER_XPRS_PARAM(XPRS_GPUPLATFORM),
    REBALANCER_XPRS_PARAM(XPRS_BARHGMINORITERATIONLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_MAXTOTALTREEFILESIZE),
    REBALANCER_XPRS_PARAM(XPRS_MIPSTOPSTAGE),
    REBALANCER_XPRS_PARAM(XPRS_GPUCUDADEVICE),
    REBALANCER_XPRS_PARAM(XPRS_ALTERNATIVELPSOLS),
    REBALANCER_XPRS_PARAM(XPRS_EXTRAELEMS),
    REBALANCER_XPRS_PARAM(XPRS_EXTRASETELEMS),
    REBALANCER_XPRS_PARAM(XPRS_BACKGROUNDSELECT),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHBACKGROUNDSELECT),
};

// Parameters from
// third-party/xpress/9.9.0/x86_64/include/xprs.h
const std::map<std::string_view, int> kDoubleParamCodes = {
    REBALANCER_XPRS_PARAM(XPRS_MAXCUTTIME),
    REBALANCER_XPRS_PARAM(XPRS_MAXSTALLTIME),
    REBALANCER_XPRS_PARAM(XPRS_TUNERMAXTIME),
    REBALANCER_XPRS_PARAM(XPRS_MATRIXTOL),
    REBALANCER_XPRS_PARAM(XPRS_PIVOTTOL),
    REBALANCER_XPRS_PARAM(XPRS_FEASTOL),
    REBALANCER_XPRS_PARAM(XPRS_OUTPUTTOL),
    REBALANCER_XPRS_PARAM(XPRS_SOSREFTOL),
    REBALANCER_XPRS_PARAM(XPRS_OPTIMALITYTOL),
    REBALANCER_XPRS_PARAM(XPRS_ETATOL),
    REBALANCER_XPRS_PARAM(XPRS_RELPIVOTTOL),
    REBALANCER_XPRS_PARAM(XPRS_MIPTOL),
    REBALANCER_XPRS_PARAM(XPRS_MIPTOLTARGET),
    REBALANCER_XPRS_PARAM(XPRS_BARPERTURB),
    REBALANCER_XPRS_PARAM(XPRS_MIPADDCUTOFF),
    REBALANCER_XPRS_PARAM(XPRS_MIPABSCUTOFF),
    REBALANCER_XPRS_PARAM(XPRS_MIPRELCUTOFF),
    REBALANCER_XPRS_PARAM(XPRS_PSEUDOCOST),
    REBALANCER_XPRS_PARAM(XPRS_PENALTY),
    REBALANCER_XPRS_PARAM(XPRS_BIGM),
    REBALANCER_XPRS_PARAM(XPRS_MIPABSSTOP),
    REBALANCER_XPRS_PARAM(XPRS_MIPRELSTOP),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERACCURACYTOL),
    REBALANCER_XPRS_PARAM(XPRS_PRIMALPERTURB),
    REBALANCER_XPRS_PARAM(XPRS_DUALPERTURB),
    REBALANCER_XPRS_PARAM(XPRS_BAROBJSCALE),
    REBALANCER_XPRS_PARAM(XPRS_BARRHSSCALE),
    REBALANCER_XPRS_PARAM(XPRS_CHOLESKYTOL),
    REBALANCER_XPRS_PARAM(XPRS_BARGAPSTOP),
    REBALANCER_XPRS_PARAM(XPRS_BARDUALSTOP),
    REBALANCER_XPRS_PARAM(XPRS_BARPRIMALSTOP),
    REBALANCER_XPRS_PARAM(XPRS_BARSTEPSTOP),
    REBALANCER_XPRS_PARAM(XPRS_ELIMTOL),
    REBALANCER_XPRS_PARAM(XPRS_MARKOWITZTOL),
    REBALANCER_XPRS_PARAM(XPRS_MIPABSGAPNOTIFY),
    REBALANCER_XPRS_PARAM(XPRS_MIPRELGAPNOTIFY),
    REBALANCER_XPRS_PARAM(XPRS_BARLARGEBOUND),
    REBALANCER_XPRS_PARAM(XPRS_PPFACTOR),
    REBALANCER_XPRS_PARAM(XPRS_REPAIRINDEFINITEQMAX),
    REBALANCER_XPRS_PARAM(XPRS_BARGAPTARGET),
    REBALANCER_XPRS_PARAM(XPRS_DUMMYCONTROL),
    REBALANCER_XPRS_PARAM(XPRS_BARSTARTWEIGHT),
    REBALANCER_XPRS_PARAM(XPRS_BARFREESCALE),
    REBALANCER_XPRS_PARAM(XPRS_SBEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_HEURDIVERANDOMIZE),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_CUTFACTOR),
    REBALANCER_XPRS_PARAM(XPRS_EIGENVALUETOL),
    REBALANCER_XPRS_PARAM(XPRS_INDLINBIGM),
    REBALANCER_XPRS_PARAM(XPRS_TREEMEMORYSAVINGTARGET),
    REBALANCER_XPRS_PARAM(XPRS_INDPRELINBIGM),
    REBALANCER_XPRS_PARAM(XPRS_RELAXTREEMEMORYLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_MIPABSGAPNOTIFYOBJ),
    REBALANCER_XPRS_PARAM(XPRS_MIPABSGAPNOTIFYBOUND),
    REBALANCER_XPRS_PARAM(XPRS_PRESOLVEMAXGROW),
    REBALANCER_XPRS_PARAM(XPRS_HEURSEARCHTARGETSIZE),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERRELPIVOTTOL),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERRELPIVOTTOLSAFE),
    REBALANCER_XPRS_PARAM(XPRS_DETLOGFREQ),
    REBALANCER_XPRS_PARAM(XPRS_MAXIMPLIEDBOUND),
    REBALANCER_XPRS_PARAM(XPRS_FEASTOLTARGET),
    REBALANCER_XPRS_PARAM(XPRS_OPTIMALITYTOLTARGET),
    REBALANCER_XPRS_PARAM(XPRS_PRECOMPONENTSEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_LPLOGDELAY),
    REBALANCER_XPRS_PARAM(XPRS_HEURDIVEITERLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_BARKERNEL),
    REBALANCER_XPRS_PARAM(XPRS_FEASTOLPERTURB),
    REBALANCER_XPRS_PARAM(XPRS_CROSSOVERFEASWEIGHT),
    REBALANCER_XPRS_PARAM(XPRS_LUPIVOTTOL),
    REBALANCER_XPRS_PARAM(XPRS_MIPRESTARTGAPTHRESHOLD),
    REBALANCER_XPRS_PARAM(XPRS_NODEPROBINGEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_INPUTTOL),
    REBALANCER_XPRS_PARAM(XPRS_MIPRESTARTFACTOR),
    REBALANCER_XPRS_PARAM(XPRS_BAROBJPERTURB),
    REBALANCER_XPRS_PARAM(XPRS_CPIALPHA),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALSPATIALBRANCHPROPAGATIONEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALSPATIALBRANCHCUTTINGEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_GLOBALBOUNDINGBOX),
    REBALANCER_XPRS_PARAM(XPRS_TIMELIMIT),
    REBALANCER_XPRS_PARAM(XPRS_SOLTIMELIMIT),
    REBALANCER_XPRS_PARAM(XPRS_REPAIRINFEASTIMELIMIT),
    REBALANCER_XPRS_PARAM(XPRS_BARHGEXTRAPOLATE),
    REBALANCER_XPRS_PARAM(XPRS_WORKLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_CALLBACKCHECKTIMEWORKDELAY),
    REBALANCER_XPRS_PARAM(XPRS_PREROOTWORKLIMIT),
    REBALANCER_XPRS_PARAM(XPRS_PREROOTEFFORT),
    REBALANCER_XPRS_PARAM(XPRS_BARHGRELTOL),
};

#else
#error \
    "Unsupported Xpress version. Please update the code to support this version."
#endif

#undef REBALANCER_XPRS_PARAM

// Throws std::runtime_error if rc != 0, appending the Xpress error string.
// Centralises the repeated XPRSgetlasterror + throw pattern across all new
// native-constraint API calls.
static void throwIfXpressError(int rc, XPRSprob xprob, const std::string& msg) {
  if (rc == 0) {
    return;
  }
  char errBuf[512] = {};
  XPRSgetlasterror(xprob, errBuf);
  throw std::runtime_error(fmt::format("{} (rc={}): {}", msg, rc, errBuf));
}

} // namespace

namespace facebook::algopt::lp::detail {

XpressProblem::XpressProblem() {
  // Forcing search to continue until optimality using default tolerances
  setTolerances(Tolerances{});
  const int scaling = getParameter("XPRS_SCALING");
  setParameter(
      "XPRS_SCALING", scaling | XPRS_SCALING_SIMPLEX_OBJECTIVE_SCALING);
}

/*
XPRB.PL continuous
XPRB.BV binary
XPRB.UI general integer
XPRB.PI partial integer
XPRB.SC semi-continuous
XPRB.SI semi-continuous integer
*/

std::shared_ptr<VariableImpl> XpressProblem::makeVar(const std::string& name) {
  return makeVar(name, XPRB_PL, -XPRB_INFINITY);
}

std::shared_ptr<VariableImpl> XpressProblem::makeIntVar(
    const std::string& name) {
  return makeVar(name, XPRB_UI, -XPRB_INFINITY);
}

std::shared_ptr<VariableImpl> XpressProblem::makeSemiContVar(
    const std::string& name,
    double threshold) {
  auto variable = makeVar(name, XPRB_SC, 0);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> XpressProblem::makeSemiIntVar(
    const std::string& name,
    double threshold) {
  auto variable = makeVar(name, XPRB_SI, 0);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> XpressProblem::makeBoolVar(
    const std::string& name) {
  return makeVar(name, XPRB_BV, 0);
}

std::shared_ptr<ExpressionImpl> XpressProblem::makeExpression(
    double constant) const {
  return std::make_shared<XpressExpression>(
      dashoptimization::XPRBexpr(constant), constant);
}

std::shared_ptr<VariableImpl>
XpressProblem::makeVar(const std::string& name, int xprbType, double lb) {
  auto variable =
      problem_.newVar(name.empty() ? nullptr : name.c_str(), xprbType, lb);
  auto getVarType = [](int xprbType) {
    switch (xprbType) {
      case XPRB_PL:
        return VariableImpl::Type::CONTINUOUS;
      case XPRB_UI:
        return VariableImpl::Type::INTEGER;
      case XPRB_SC:
        return VariableImpl::Type::SEMI_CONTINUOUS;
      case XPRB_SI:
        return VariableImpl::Type::SEMI_INTEGER;
      case XPRB_BV:
        return VariableImpl::Type::BINARY;
      default:
        throw std::runtime_error(
            fmt::format("unknown xpress variable type {}", xprbType));
    }
  };
  return std::make_shared<XpressVariable>(variable, getVarType(xprbType));
}

std::shared_ptr<ConstraintImpl> XpressProblem::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) {
  auto constraint = problem_.newCtr(
      name.c_str(), dynamic_cast<const XpressRelation&>(*relation).get());
  return std::make_shared<XpressConstraint>(constraint);
}

void XpressProblem::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) {
  problem_.delCtr(dynamic_cast<XpressConstraint&>(*constraint).get());
}

int XpressProblem::getObjectiveSize() const {
  return objectives_.size();
}

std::shared_ptr<const ExpressionImpl> XpressProblem::getObjectiveAt(
    int pos) const {
  if (pos >= getObjectiveSize()) {
    throw std::runtime_error(
        fmt::format(
            "There are only {} objectives, but attempt to access objective at pos {}",
            getObjectiveSize(),
            pos));
  }
  return objectives_.at(pos);
}

void XpressProblem::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  objectives_.push_back(expression);
}

void XpressProblem::clearObjectives() {
  objectives_.clear();
  // also clear all saved results w.r.t. those objectives
  problemResultPerObjective_.clear();
}

void XpressProblem::setObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  problem_.setObj(dynamic_cast<const XpressExpression&>(*expression).get());
}

void XpressProblem::setTolerances(const Tolerances& tol) {
  setParameter("XPRS_FEASTOL", tol.constraint);
  setParameter("XPRS_MIPTOL", tol.integer);
  setParameter("XPRS_MIPRELSTOP", tol.relgap);
  setParameter("XPRS_MIPABSSTOP", tol.absgap);
  setParameter("XPRS_MIPRELCUTOFF", tol.relcut);
  setParameter("XPRS_MIPADDCUTOFF", tol.abscut);
}

Tolerances XpressProblem::getTolerances() {
  return {
      .constraint = getParameter("XPRS_FEASTOL"),
      .integer = getParameter("XPRS_MIPTOL"),
      .absgap = getParameter("XPRS_MIPABSSTOP"),
      .relgap = getParameter("XPRS_MIPRELSTOP"),
      .abscut = getParameter("XPRS_MIPADDCUTOFF"),
      .relcut = getParameter("XPRS_MIPRELCUTOFF")};
}

int XpressProblem::callbackImpl(XPRSprob& prob) {
  if (callbackAborted_) {
    return 1;
  }
  double bound;
  double objective;
  XPRSgetdblattrib(prob, XPRS_BESTBOUND, &bound);
  XPRSgetdblattrib(prob, XPRS_MIPBESTOBJVAL, &objective);
  if ((*callback_)(
          {.bound = bound,
           .objective = objective,
           .walltime = timer_.getSeconds()}) == ProblemCallbackAction::ABORT) {
    callbackAborted_ = true;
    return 1;
  }
  return 0;
}

/* static */
int XpressProblem::customCallback(XPRSprob prob, void* data) {
  auto xpressProblem = (XpressProblem*)data;
  return xpressProblem->callbackImpl(prob);
}

void XpressProblem::solveForObjectiveAt(
    int pos,
    std::optional<double> timeLimit) {
  // set objective
  auto objective = getObjectiveAt(pos);
  setObjective(objective);

  // set timeLimit if given
  if (timeLimit.has_value()) {
    setTimeout(timeLimit.value());
  }

  // set the parameter for objective at position pos
  setMultiObjectiveParameter(pos);

  // start the timer as we start solve
  timer_.start();

  // Prevent error "problem has no variables" by adding a dummy constraint with
  // a variable.
  problem_.newCtr(nullptr, problem_.newVar() == 0);

  auto lpConfigs = getAlgorithm();
  // Provide initial assignment as a warm-start hint via XPRSaddmipsol.
  // Note: when native PWL/Max constraints are used, the warm-start only covers
  // the original model variables. Auxiliary variables introduced by
  // addNativePwlConstraint/addNativeMaxConstraint (pwl_x, pwl_y, max_result,
  // max_input_*) are absent from the hint; Xpress will complete or discard
  // the partial warm-start at its discretion.
  if (!initialValues_.empty()) {
    XLOG(DBG1) << "Adding warm-start solution from initial values";
    saveDebugData(DebugPhase::PreWarmStart);
    addMipSolFromInitialValues();
    saveDebugData(DebugPhase::PostWarmStart);
    XLOG(DBG1) << "Warm-start solution added";
  }

  XLOG(DBG1) << "Starting main assignment solve";
  callbackAborted_ = false;
  if (callback_) {
    XPRSaddcbchecktime(
        problem_.getXPRSprob(),
        customCallback,
        this, // user defined data
        1 // priority
    );
  }

  // Perform main solve. When native PWL/Max constraints are registered, we
  // must call loadMat() first to get valid column indices, apply the C API
  // constraints (XPRSaddpwlcons / XPRSaddgencons), then call XPRSmipoptimize
  // directly — bypassing BCL's mipOptimize which would call loadMat() again
  // and wipe the C API additions.
  saveDebugData(DebugPhase::PreMain);
  if (usesNativeExpressions()) {
    problem_.loadMat();
    applyNativeConstraints(problem_.getXPRSprob());
    XPRSprob xprob = problem_.getXPRSprob();
    throwIfXpressError(
        XPRSmipoptimize(xprob, lpConfigs.c_str()),
        xprob,
        "XPRSmipoptimize failed");
    // XPRBgetsol reads BCL's internal solution cache, which is only populated
    // by BCL's own solve path. After a direct XPRSmipoptimize call, we must
    // explicitly load the Xpress MIP solution into BCL so XPRBgetsol works.
    int mipStatus = XPRS_MIP_INFEAS;
    throwIfXpressError(
        XPRSgetintattrib(xprob, XPRS_MIPSTATUS, &mipStatus),
        xprob,
        "XPRSgetintattrib(XPRS_MIPSTATUS) failed");
    if (mipStatus == XPRS_MIP_OPTIMAL || mipStatus == XPRS_MIP_SOLUTION) {
      int ncols = 0;
      throwIfXpressError(
          XPRSgetintattrib(xprob, XPRS_COLS, &ncols),
          xprob,
          "XPRSgetintattrib(XPRS_COLS) failed");
      if (ncols > 0) {
        std::vector<double> solValues(ncols, 0.0);
        // XPRSgetmipsol is deprecated since v44.00; XPRSgetsolution returns
        // the same MIP solution for columns [0, ncols).
        throwIfXpressError(
            XPRSgetsolution(xprob, nullptr, solValues.data(), 0, ncols - 1),
            xprob,
            fmt::format("XPRSgetsolution failed (ncols={})", ncols));
        const bool isOptimal = (mipStatus == XPRS_MIP_OPTIMAL);
        problem_.loadMIPSol(solValues.data(), ncols, isOptimal);
      }
    } else {
      // No incumbent found (infeasible, time-limit, unbounded, etc.).
      // BCL's solution cache is not updated — callers must check
      // XPRS_MIPSTATUS or the solver result status directly rather than
      // relying on BCL's XPRBgetsol for outcome detection.
      XLOG(DBG1) << fmt::format(
          "Native-constraint solve: no incumbent (XPRS_MIPSTATUS={}); "
          "BCL solution cache left unsynced",
          mipStatus);
    }
  } else {
    problem_.mipOptimize(lpConfigs.c_str());
  }
  saveDebugData(DebugPhase::PostMain);

  timer_.stop();

  if (callback_) {
    XPRSremovecbchecktime(
        problem_.getXPRSprob(),
        customCallback,
        this // user defined data
    );
  }
  XLOG(DBG1) << "Completed main assignment solve";
}

std::string XpressProblem::getAlgorithm() {
  std::string lpConfigs;
  const int32_t algorithm = getParameter("XPRS_DEFAULTALG");
  switch (algorithm) {
    case 1:
      lpConfigs = "c"; // automatic
      break;
    case 2:
      lpConfigs = "d"; // dual simplex
      break;
    case 3:
      lpConfigs = "p"; // primal simplex
      break;
    case 4:
      lpConfigs = "b"; // newton-barrier
      break;
    default:
      lpConfigs = "c"; // automatic
  }
  return lpConfigs;
}

thrift::ProblemStatus XpressProblem::getSolveStatus() {
  int status;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_MIPSTATUS, &status);
  switch (status) {
    case XPRS_MIP_OPTIMAL:
      return thrift::ProblemStatus::OPTIMAL_FOUND;
    case XPRS_MIP_SOLUTION:
      return thrift::ProblemStatus::SOLUTION_FOUND;
    case XPRS_MIP_INFEAS:
    case XPRS_MIP_UNBOUNDED:
      return thrift::ProblemStatus::NO_SOLUTION_EXISTS;
    // XPRS_MIP_LP_OPTIMAL: Global search incomplete - the initial continuous
    // relaxation has been solved and no integer solution has been found.
    case XPRS_MIP_LP_OPTIMAL:
    case XPRS_MIP_NO_SOL_FOUND:
    case XPRS_MIP_LP_NOT_OPTIMAL:
      return thrift::ProblemStatus::SOLUTION_NOT_FOUND;
    default:
      throw std::runtime_error(
          fmt::format("unknown xpress status code {}", status));
  }
}

thrift::ProblemResult XpressProblem::getSolveResult() {
  thrift::ProblemResult result;
  result.status() = getSolveStatus();
  if (warmStartResult_.has_value()) {
    result.warmStartResult() = warmStartResult_.value();
  }

  double bestBound;
  XPRSgetdblattrib(problem_.getXPRSprob(), XPRS_BESTBOUND, &bestBound);
  double bestSolution;
  XPRSgetdblattrib(problem_.getXPRSprob(), XPRS_MIPBESTOBJVAL, &bestSolution);

  result.bestBound() = bestBound;
  result.bestObjective() = bestSolution;
  result.absoluteGap() = bestSolution - bestBound;
  result.problemAttributes() = getProblemAttributes();
  // as defined in https://fburl.com/iigtjzpf xpress library
  const double denominator =
      std::max(std::abs(bestBound), std::abs(bestSolution));
  if (denominator == 0) {
    result.relativeGap() = 0;
  } else {
    result.relativeGap() = std::abs(bestSolution - bestBound) / denominator;
  }
  return result;
}

thrift::ProblemAttributes XpressProblem::getProblemAttributes() {
  int xprsCols;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_COLS, &xprsCols);
  int xprsRows;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_ROWS, &xprsRows);
  int xprsElems;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_ELEMS, &xprsElems);

  thrift::ProblemAttributes problemAttributes;
  // Xpress adds a dummy row, col and non-zero entry internally
  problemAttributes.numVariables() = xprsCols > 0 ? xprsCols - 1 : xprsCols;
  problemAttributes.numConstraints() = xprsRows > 0 ? xprsRows - 1 : xprsRows;
  problemAttributes.numOfNonZeros() = xprsElems > 0 ? xprsElems - 1 : xprsElems;
  // xpress does not seem to have the notion of`modelFingerprint`
  return problemAttributes;
}

double XpressProblem::getParameter(const std::string& name) {
  const auto intParam = getIntParamCode(name);
  if (intParam) {
    int value;
    if (XPRSgetintcontrol(problem_.getXPRSprob(), *intParam, &value)) {
      throw std::runtime_error(
          fmt::format("could not get int parameter {} {}", name, *intParam));
    }
    return value;
  }
  const auto doubleParam = getDoubleParamCode(name);
  if (doubleParam) {
    double value;
    if (XPRSgetdblcontrol(problem_.getXPRSprob(), *doubleParam, &value)) {
      throw std::runtime_error(
          fmt::format(
              "could not get double parameter {} {}", name, *doubleParam));
    }
    return value;
  }
  throw std::runtime_error(fmt::format("unknown parameter {}", name));
}

void XpressProblem::setMultiObjectiveParameter(int index) {
  if (multiObjConfig_) {
    for (auto& [key, val] : (multiObjConfig_->paramNamesToValues)) {
      setParameter(
          key, algopt::common::thriftUtils::getObjectiveValue(index, val));
    }
  }
}

void XpressProblem::setParameter(const std::string& name, double value) {
  double oldValue = getParameter(name);
  const auto intParam = getIntParamCode(name);
  if (intParam) {
    if (XPRSsetintcontrol(
            problem_.getXPRSprob(), *intParam, static_cast<int>(value))) {
      throw std::runtime_error(
          fmt::format("could not set int parameter {} {}", name, *intParam));
    }
    XLOG(INFO) << fmt::format(
        "Updating XPRESS param {} from {} to {}", name, oldValue, value);
    return;
  }
  const auto doubleParam = getDoubleParamCode(name);
  if (doubleParam) {
    if (XPRSsetdblcontrol(problem_.getXPRSprob(), *doubleParam, value)) {
      throw std::runtime_error(
          fmt::format(
              "could not set double parameter {} {}", name, *doubleParam));
    }
    XLOG(INFO) << fmt::format(
        "Updating XPRESS param {} from {} to {}", name, oldValue, value);
    return;
  }
  throw std::runtime_error(fmt::format("unknown parameter {}", name));
}

void XpressProblem::setLogfile(const std::string& filename) {
  XPRSsetlogfile(problem_.getXPRSprob(), filename.c_str());
}

void XpressProblem::saveToFile(const std::string& filename) {
  const std::filesystem::path path(filename);
  auto extension = path.extension();
  auto extensionless = path.parent_path() / path.stem();

  if (extension == ".lp") {
    problem_.exportProb(XPRB_LP, extensionless.c_str());
  } else if (extension == ".mps") {
    problem_.exportProb(XPRB_MPS, extensionless.c_str());
  } else if (extension == ".svf") {
    XPRSsaveas(problem_.getXPRSprob(), extensionless.c_str());
  } else {
    throw std::runtime_error(
        "File type not supported, for Xpress, expected .lp/.mps/.svf");
  }
}

void XpressProblem::saveToFileWithObjectiveAt(
    int pos,
    const std::string& filename) {
  setObjective(objectives_.at(pos));
  saveToFile(filename);
}

void XpressProblem::saveToFileWithAllObjectives(const std::string& filename) {
  if (!objectives_.empty()) {
    // Xpress's native model only holds a single objective at a time;
    // customMultiObjectiveIterativeSolve() handles multi-objective by repeated
    // single-objective solves with added constraints. For an export snapshot
    // we install only the highest-priority (first) objective.
    setObjective(objectives_.at(0));
  }
  saveToFile(filename);
}

void XpressProblem::setDebugPath(const std::string& path) {
  debugPath_ = path;
}

void XpressProblem::print(
    const std::vector<std::string>& /*substringsToMatch*/) {
  problem_.print();
}

void XpressProblem::disableLogs() {
  loggingDisabled_ = true;
  setParameter("XPRS_OUTPUTLOG", 0);
}

bool XpressProblem::isLoggingDisabled() const {
  return loggingDisabled_;
}

void XpressProblem::saveDebugData(DebugPhase phase) {
  if (!debugPath_) {
    return;
  }

  std::string solveName;
  switch (phase) {
    case DebugPhase::PreWarmStart:
    case DebugPhase::PostWarmStart:
      solveName = "warm-start";
      break;
    case DebugPhase::PreMain:
    case DebugPhase::PostMain:
      solveName = "main";
      break;
  }

  std::string stage;
  switch (phase) {
    case DebugPhase::PreWarmStart:
    case DebugPhase::PreMain:
      stage = "pre";
      break;
    case DebugPhase::PostWarmStart:
    case DebugPhase::PostMain:
      stage = "post";
      break;
  }

  if (phase == DebugPhase::PreWarmStart || phase == DebugPhase::PreMain) {
    auto filePath = fmt::format("{}/{}", *debugPath_, solveName);
    problem_.exportProb(XPRB_LP, filePath.c_str());
    problem_.exportProb(XPRB_MPS, filePath.c_str());
  }

  {
    auto filePath = fmt::format("{}/{}-{}", *debugPath_, stage, solveName);
    XPRSsaveas(problem_.getXPRSprob(), filePath.c_str());
  }
}

void XpressProblem::setTimeout(double solveTime) {
  setParameter("XPRS_TIMELIMIT", solveTime);
}

void XpressProblem::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) {
  initialValues_[variable] = value;
}

/* static */ thrift::WarmStartStatus XpressProblem::getWarmStartStatus(
    int32_t processingStatus) {
  // refer to https://fburl.com/gqjj8pkw for the different processing
  // status values. See also XPRS_USERSOLSTATUS_* constants in xprs.h.
  thrift::WarmStartStatus warmStartStatus;
  switch (processingStatus) {
    case XPRS_USERSOLSTATUS_NOT_CHECKED:
      warmStartStatus = thrift::WarmStartStatus::PROCESSING_ERROR;
      break;
    case XPRS_USERSOLSTATUS_ACCEPTED_FEASIBLE:
      warmStartStatus = thrift::WarmStartStatus::FEASIBLE_SOLUTION_PROVIDED;
      break;
    case XPRS_USERSOLSTATUS_ACCEPTED_OPTIMIZED:
    case XPRS_USERSOLSTATUS_SEARCHED_SOL:
      warmStartStatus = thrift::WarmStartStatus::FEASIBLE_SOLUTION_OBTAINED;
      break;
    case XPRS_USERSOLSTATUS_SEARCHED_NOSOL:
    case XPRS_USERSOLSTATUS_REJECTED_INFEAS_NOSEARCH:
    case XPRS_USERSOLSTATUS_REJECTED_PARTIAL_NOSEARCH:
    case XPRS_USERSOLSTATUS_REJECTED_FAILED_OPTIMIZE:
    case XPRS_USERSOLSTATUS_REJECTED_CUTOFF:
      warmStartStatus = thrift::WarmStartStatus::FEASIBLE_SOLUTION_NOT_FOUND;
      break;
    case XPRS_USERSOLSTATUS_DROPPED:
      warmStartStatus = thrift::WarmStartStatus::SOLUTION_IS_DROPPED;
      break;
    default:
      throw std::runtime_error(
          fmt::format(
              "Unknown xpress status code {} for warm-start processing",
              processingStatus));
  }

  return warmStartStatus;
}

/* static */ void XpressProblem::callbackForInitialSolution(
    XPRSprob /* xprsProblem */,
    void* xpressProblem,
    const char* solutionName,
    int32_t processingStatus) {
  auto* xpressProblemPtr = static_cast<XpressProblem*>(xpressProblem);
  thrift::WarmStartResult warmResult;
  warmResult.status() = getWarmStartStatus(processingStatus);
  warmResult.processingTimeInSecs() = xpressProblemPtr->timer_.getSeconds();
  xpressProblemPtr->warmStartResult_ = warmResult;

  auto warmStartMsg = fmt::format(
      "Initial solution supplied through warm-start has been processed."
      " Processing Status: {}, Solution Name: {} ",
      processingStatus,
      solutionName);
  if (xpressProblemPtr->isLoggingDisabled()) {
    // if logging was disabled, only print this information in debug mode
    XLOG(DBG) << warmStartMsg;
  } else {
    XLOG(INFO) << warmStartMsg;
  }
}

void XpressProblem::applyNativeConstraints(XPRSprob xprob) {
  // Called after loadMat() finalizes column indices, once per objective solve.
  // XPRSaddpwlcons / XPRSaddgencons are additive, so first remove our own
  // constraints left over from a previous solve to keep re-application
  // idempotent. We track both the count and the starting index recorded when
  // the constraints were first added (ownedPwl/GenConsStartIdx_) so that
  // deletion targets exactly our additions regardless of what other code paths
  // may have added before ours.
  //
  // Index-stability assumptions this deletion-by-range relies on:
  //  (a) No other code path adds or deletes PWL/general constraints between the
  //      previous solve and this call, so our additions remain the contiguous
  //      tail [ownedStartIdx, ownedStartIdx + nOwned).
  //  (b) Xpress preserves stable, contiguous indices for surviving constraints
  //      across XPRSdelpwlcons / XPRSdelgencons (i.e. it does not gap-fill or
  //      renumber the constraints we did not delete). Since we only ever delete
  //      our own tail range and re-add immediately afterwards, no surviving
  //      constraint is renumbered in practice.
  if (nOwnedPwlCons_ > 0) {
    std::vector<int> pwlInd(nOwnedPwlCons_);
    std::iota(pwlInd.begin(), pwlInd.end(), ownedPwlConsStartIdx_);
    const int delRc = XPRSdelpwlcons(xprob, nOwnedPwlCons_, pwlInd.data());
    throwIfXpressError(
        delRc,
        xprob,
        fmt::format(
            "XPRSdelpwlcons failed (nOwnedPwlCons={})", nOwnedPwlCons_));
    nOwnedPwlCons_ = 0;
  }
  if (nOwnedGenCons_ > 0) {
    std::vector<int> genInd(nOwnedGenCons_);
    std::iota(genInd.begin(), genInd.end(), ownedGenConsStartIdx_);
    throwIfXpressError(
        XPRSdelgencons(xprob, nOwnedGenCons_, genInd.data()),
        xprob,
        fmt::format(
            "XPRSdelgencons failed (nOwnedGenCons={})", nOwnedGenCons_));
    nOwnedGenCons_ = 0;
  }

  applyNativePwlConstraints(xprob);
  applyNativeMaxConstraints(xprob);
}

void XpressProblem::applyNativePwlConstraints(XPRSprob xprob) {
  // Record the starting index before our additions so applyNativeConstraints
  // can delete exactly [ownedPwlConsStartIdx_, ownedPwlConsStartIdx_ +
  // nOwnedPwlCons_) on the next re-application. This relies on no other code
  // path adding PWL constraints between this add and the subsequent delete.
  throwIfXpressError(
      XPRSgetintattrib(xprob, XPRS_PWLCONS, &ownedPwlConsStartIdx_),
      xprob,
      "XPRSgetintattrib(XPRS_PWLCONS) failed");
  for (const auto& spec : nativePwlConstraints_) {
    const auto* xVar = dynamic_cast<const XpressVariable*>(spec.xVar.get());
    const auto* yVar = dynamic_cast<const XpressVariable*>(spec.yVar.get());
    if (xVar == nullptr || yVar == nullptr) {
      throw std::runtime_error(
          "Native PWL constraint requires XpressVariable operands, but a "
          "non-Xpress VariableImpl was provided");
    }
    int xcol = xVar->get().getColNum();
    int ycol = yVar->get().getColNum();
    const int npoints = static_cast<int>(spec.points.size());
    // CSR-style start offsets: start[0]=0, start[1]=npoints (1 constraint).
    const int start[2] = {0, npoints};
    std::vector<double> xvals, yvals;
    xvals.reserve(npoints);
    yvals.reserve(npoints);
    for (const auto& [xi, yi] : spec.points) {
      xvals.push_back(xi);
      yvals.push_back(yi);
    }
    throwIfXpressError(
        XPRSaddpwlcons(
            xprob, 1, npoints, &xcol, &ycol, start, xvals.data(), yvals.data()),
        xprob,
        fmt::format("XPRSaddpwlcons failed (xcol={}, ycol={})", xcol, ycol));
    ++nOwnedPwlCons_;
  }
}

void XpressProblem::applyNativeMaxConstraints(XPRSprob xprob) {
  // Record the starting index before our additions so applyNativeConstraints
  // can delete exactly these constraints on the next re-application.
  throwIfXpressError(
      XPRSgetintattrib(xprob, XPRS_GENCONS, &ownedGenConsStartIdx_),
      xprob,
      "XPRSgetintattrib(XPRS_GENCONS) failed");
  for (const auto& spec : nativeMaxConstraints_) {
    const auto* resultVar =
        dynamic_cast<const XpressVariable*>(spec.resultVar.get());
    if (resultVar == nullptr) {
      throw std::runtime_error(
          "Native MAX constraint requires an XpressVariable result, but a "
          "non-Xpress VariableImpl was provided");
    }
    const int resultCol = resultVar->get().getColNum();
    std::vector<int> inputCols;
    inputCols.reserve(spec.inputVars.size());
    for (const auto& inputImpl : spec.inputVars) {
      const auto* inputVar =
          dynamic_cast<const XpressVariable*>(inputImpl.get());
      if (inputVar == nullptr) {
        throw std::runtime_error(
            "Native MAX constraint requires XpressVariable inputs, but a "
            "non-Xpress VariableImpl was provided");
      }
      inputCols.push_back(inputVar->get().getColNum());
    }
    // CSR-style column offsets: colstart[0]=0, colstart[1]=nInputs (1
    // constraint).
    const int colstart[2] = {0, static_cast<int>(inputCols.size())};
    const int contype = XPRS_GENCONS_MAX;
    throwIfXpressError(
        XPRSaddgencons(
            xprob,
            1,
            static_cast<int>(inputCols.size()),
            0,
            &contype,
            &resultCol,
            colstart,
            inputCols.data(),
            nullptr,
            nullptr),
        xprob,
        fmt::format(
            "XPRSaddgencons failed (resultCol={}, nInputs={})",
            resultCol,
            inputCols.size()));
    ++nOwnedGenCons_;
  }
}

void XpressProblem::addMipSolFromInitialValues() {
  // Ensure the problem matrix is loaded so variables have column indices
  problem_.loadMat();

  std::vector<double> sol;
  std::vector<int> idxs;
  sol.reserve(initialValues_.size());
  idxs.reserve(initialValues_.size());

  for (const auto& [variable, value] : initialValues_) {
    const auto& xpressVar =
        (dynamic_cast<const XpressVariable*>(variable.get()))->get();
    idxs.push_back(xpressVar.getColNum());
    sol.push_back(value);
  }

  // Add initial solution as a MIP start hint
  XPRSaddmipsol(
      problem_.getXPRSprob(),
      static_cast<int>(sol.size()),
      sol.data(),
      idxs.data(),
      "initial_assignment");

  // Callback to retrieve status when the initial solution is processed
  XPRSaddcbusersolnotify(
      problem_.getXPRSprob(),
      callbackForInitialSolution,
      (void*)this, // user defined data
      0 // priority
  );
}

std::optional<int> XpressProblem::getIntParamCode(const std::string& name) {
  return folly::get_optional(kIntParamCodes, name).toStdOptional();
}

std::optional<int> XpressProblem::getDoubleParamCode(const std::string& name) {
  return folly::get_optional(kDoubleParamCodes, name).toStdOptional();
}

void XpressProblem::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback) {
  callback_ = callback;
}

std::optional<IIS> XpressProblem::getIIS() {
  XLOG(WARNING) << "getIIS has not been implemented in XPRESS.";
  return {};
}

void XpressProblem::replay(const std::string& /* fileName */) const {
  throw std::runtime_error("read from file is not implemented");
  // Unfortunately, the Xpress C++ API does not seem to provide a direct way to
  // do this
  // https://www.fico.com/fico-xpress-optimization/docs/dms2021-01/bcl/dhtml/Cpp-XPRBprob.html
  // In particular, following could work except it needs a C style struct
  // pointer, so reasonable effort is required to make this work which we will
  // revisit if need be (TODO: @nks)
  //  XPRSreadprob(prob, "problem_from_file", "v")
}

bool XpressProblem::supportsNativeQuadratic() const {
  return algopt::useXpressNativeQuadratic();
}

bool XpressProblem::supportsNativePwl() const {
  return algopt::useXpressNativePwl();
}

bool XpressProblem::supportsNativeMax() const {
  return algopt::useXpressNativeMax();
}

bool XpressProblem::usesNativeExpressions() const {
  return !nativePwlConstraints_.empty() || !nativeMaxConstraints_.empty();
}

bool XpressProblem::supportsIndicatorConstraints() const {
  return algopt::useXpressIndicatorConstraints();
}

bool XpressProblem::setIndicatorOnConstraint(
    Constraint& ctr,
    const Variable& binaryVar,
    int dir) {
  auto* xpressCtr = dynamic_cast<XpressConstraint*>(ctr.get().get());
  const auto* xpressVar =
      dynamic_cast<const XpressVariable*>(binaryVar.get().get());
  if (!xpressCtr || !xpressVar) {
    return false;
  }
  // Indicator constraints require a binary variable (XPRB_BV type with
  // bounds [0, 1]). Reject anything else to prevent silent mis-formulations.
  if (xpressVar->get().getType() != XPRB_BV) {
    throw std::invalid_argument(
        fmt::format(
            "setIndicatorOnConstraint: binaryVar must be a binary variable "
            "(XPRB_BV), got type {}",
            xpressVar->get().getType()));
  }
  return xpressCtr->get().setIndicator(dir, xpressVar->get()) == 0;
}

std::optional<algopt::lp::Expression> XpressProblem::addNativePwlConstraint(
    const algopt::lp::Expression& x,
    const std::vector<std::pair<double, double>>& points) {
  // Create auxiliary input variable x_aux and link it to x via BCL constraint.
  // XPRSaddpwlcons requires the input variable to be within the breakpoint
  // range, so we set explicit bounds. Column indices are invalid until
  // loadMat(); we defer the XPRSaddpwlcons call to the presolve callback.
  if (points.size() < 2) {
    throw std::invalid_argument(
        fmt::format(
            "PWL breakpoints must have at least 2 points, got {}",
            points.size()));
  }
  for (const auto i : folly::irange(size_t{1}, points.size())) {
    if (points[i].first <= points[i - 1].first) {
      throw std::invalid_argument(
          fmt::format(
              "PWL breakpoints must be strictly increasing by x: x[{}]={} <= x[{}]={}",
              i,
              points[i].first,
              i - 1,
              points[i - 1].first));
    }
  }
  auto xAuxImpl = makeVar("pwl_x");
  auto yImpl = makeVar("pwl_y");

  // Bound x_aux to the PWL breakpoint range so Xpress doesn't declare the
  // problem infeasible due to the input variable potentially being
  // out-of-range. The equality constraint x_aux == x (added below) therefore
  // also constrains x itself to this range — unlike the Big-M
  // convex-combination encoding, which clips out-of-range values to the
  // boundary. IMPORTANT: callers must guarantee that x cannot reach a value
  // outside [points.front().x, points.back().x] at any feasible solution.
  // Static expression bounds (from lowerAndUpperBounds) are a necessary but
  // not sufficient check — additional model constraints could push x outside
  // the breakpoint domain at runtime, silently converting a feasible problem
  // to infeasible. Do NOT invoke this path when such runtime exceedance is
  // possible.
  xAuxImpl->setLB(points.front().first);
  xAuxImpl->setUB(points.back().first);

  double yMin = points.front().second;
  double yMax = points.front().second;
  for (const auto& [xi, yi] : points) {
    yMin = std::min(yMin, yi);
    yMax = std::max(yMax, yi);
  }
  yImpl->setLB(yMin);
  yImpl->setUB(yMax);

  auto xAuxExprImpl = xAuxImpl->makeExpression(1.0);
  auto xNegImpl = x.get()->clone();
  xNegImpl->multiply(-1.0);
  xAuxExprImpl->add(xNegImpl);
  newConstraint(xAuxExprImpl->makeEqualZeroRelation(), "pwl_x_link");

  nativePwlConstraints_.push_back({xAuxImpl, yImpl, points});
  return algopt::lp::Expression(yImpl->makeExpression(1.0));
}

std::optional<algopt::lp::Expression> XpressProblem::addNativeMaxConstraint(
    const std::vector<algopt::lp::Expression>& inputs) {
  if (inputs.empty()) {
    throw std::invalid_argument(
        "MAX constraint requires at least 1 input expression");
  }
  // Create auxiliary input variables linked to each input expression, and a
  // result variable. The XPRSaddgencons call is deferred to
  // solveForObjectiveAt() after loadMat() makes column indices valid.
  // makeVar() for a continuous variable defaults to LB=-XPRB_INFINITY, so
  // max_result is already correctly unbounded from below (negative inputs
  // work).
  // TODO: tighten the UB to max(ub_i) across inputs once LP expression bound
  // tracking is available, to strengthen the LP relaxation.
  auto resultImpl = makeVar("max_result");

  std::vector<std::shared_ptr<VariableImpl>> inputVarImpls;
  inputVarImpls.reserve(inputs.size());
  for (const auto i : folly::irange(inputs.size())) {
    auto inputImpl = makeVar(fmt::format("max_input_{}", i));
    auto inputAuxExprImpl = inputImpl->makeExpression(1.0);
    auto inputNegImpl = inputs[i].get()->clone();
    inputNegImpl->multiply(-1.0);
    inputAuxExprImpl->add(inputNegImpl);
    newConstraint(
        inputAuxExprImpl->makeEqualZeroRelation(),
        fmt::format("max_input_link_{}", i));
    inputVarImpls.push_back(inputImpl);
  }

  nativeMaxConstraints_.push_back({resultImpl, std::move(inputVarImpls)});
  return algopt::lp::Expression(resultImpl->makeExpression(1.0));
}

} // namespace facebook::algopt::lp::detail

#endif
