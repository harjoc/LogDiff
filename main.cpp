#include <QApplication>
#include "logdiff.h"

#pragma warning(disable:4996)

int main(int argc, char *argv[])
{
    freopen("logdiff.log", "wb", stdout);
    setbuf(stdout, NULL);

    QApplication a(argc, argv);
    LogDiff w;
    w.show();
    
    return a.exec();
}
