
// NOTE: This is a test file for ofmt. Not part of the project,
// only for manual development/testing purposes.

#include <iostream>
#include <iomanip>
#include "ofmt.h"
#include "ofmt_iostream.h"

using namespace std;
using namespace hvu;

int main( int argc, char** argv )
{
    ofmt_refs sout(std::cout);

    sout.printl("String one: ", fmt(std::string("Temporary String")), " - string 3"_SV);

    //std::stringstream ob;
    hvu::ofmt_bufx ob;
    //ob << fmtc().hex();
    ob << fmtx(hex);
    int x = 0x20, y = 0x30;

    // PRINTS:20 30
    ob << x << " " << y << "\n";

    // PRINTS:32 30
    ob << fmt(x) << " " << fmtx(y) << "\n";

    // PRINTS:  0032 30
    ob << fmt(x, fmtc().width(4).fillzero()) << " " << y << "\n";

    // PRINTS:  0020 30
    ob << fmtx(x, fmtc().width(4).fillzero()) << " " << y << "\n";

    ob << setw(4) << setfill('0') << fmtx(x) << " " << fmtx(y) << endl;

    cout << ob.str();

    cout << hex;
    ofprintxl(cout, x, " ", fmtm(y, oct, left, setw(8), setfill('.')));
    ofprintxl(cout, x, " ", fmtm(y, oct));

    return 0;
}
