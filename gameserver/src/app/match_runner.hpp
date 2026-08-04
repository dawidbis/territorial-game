#pragma once

#include "app/options.hpp"
#include "tick/match_lifetime.hpp"

#include <boost/asio/awaitable.hpp>

namespace gs
{

struct MatchServices;
struct MatchSetup;
class MatchClock;

/// Cały mecz w jednej korutynie: nasłuch, sygnały, pętla tików i sprzątanie.
///
/// Opcje **przez wartość**, nie referencję: korutyna żyje dłużej niż wywołanie, które ją
/// utworzyło. Reszta przez referencję świadomie — wszystko to żyje w `main`, czyli dłużej niż
/// `io_context`, który tę korutynę wznawia. Sesje potrafią przeżyć koniec tej pętli o kilka
/// wznowień, więc trzymanie ich zależności w ramce korutyny byłoby dostępem do zwolnionej
/// pamięci.
boost::asio::awaitable<MatchOutcome> run_match(
    Options options,
    MatchServices& services,
    MatchClock& clock,
    MatchSetup& setup);

} // namespace gs
