#include "logdiff.h"
#include "ui_logdiff.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/*

- if you select multiple files in 'open #1', use 2nd as file #2

*/

LogDiff::LogDiff(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::LogDiff)
{
    ui->setupUi(this);

    QTableWidget *t = ui->threadsTable;

    QStringList cols = QStringList() <<
            "PID1" << "PID2" <<
            "TID1" << "TID2" <<
            "Lines1" << "Lines2" <<
            "Similarity" << "First line";

    int widths[] = {40, 40, 40, 40, 45, 45, 48, 48, 30};

    t->setColumnCount(cols.size());
    t->setHorizontalHeaderLabels(cols);

    for (int i=0; i<cols.size(); i++) {
         t->horizontalHeaderItem(i)->setTextAlignment(Qt::AlignLeft);
         t->setColumnWidth(i, widths[i]);
    }

    #if 1
    ui->log1Edit->setText("D:/dev/logdiff/inputs/s1.csv");
    ui->log2Edit->setText("D:/dev/logdiff/inputs/s2.csv");
    #endif
}

LogDiff::~LogDiff()
{
    clearSession();
    delete ui;
}

void LogDiff::on_browse1Btn_clicked()
{
    QString fname = QFileDialog::getOpenFileName(this, "Open log file #1", QString());
    if (fname.isNull())
        return;

    ui->log1Edit->setText(fname);
    processLogs();
}

void LogDiff::on_browse2Btn_clicked()
{
    QString fname = QFileDialog::getOpenFileName(this, "Open log file #2", QString());
    if (fname.isNull())
        return;

    ui->log2Edit->setText(fname);
    processLogs();
}

void LogDiff::on_log1Edit_returnPressed()
{
    processLogs();
}

void LogDiff::on_log2Edit_returnPressed()
{
    processLogs();
}

void LogDiff::error(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text, QMessageBox::Ok);
}

#ifdef _WIN32

int gettimeofday(struct timeval *tv)
{
    FILETIME        ft;
    LARGE_INTEGER   li;
    __int64         t;

    GetSystemTimeAsFileTime(&ft);
    li.LowPart  = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    t  = li.QuadPart;           /* In 100-nanosecond intervals */
    t -= 116444736000000000i64; /* Offset to the Epoch time */
    t /= 10;                    /* In microseconds */
    tv->tv_sec  = (long)(t / 1000000);
    tv->tv_usec = (long)(t % 1000000);

    return 0;
}

#endif // _WIN32

void LogDiff::clearSession()
{
    if (sessionDir.isEmpty())
        return;

    foreach (QString id, ids1) {
        QDir(sessionDir).remove(QString("0-%1.csv").arg(id));
        QDir(sessionDir).remove(QString("0-%1.match").arg(id));
    }
    foreach (QString id, ids2) {
        QDir(sessionDir).remove(QString("1-%1.csv").arg(id));
        QDir(sessionDir).remove(QString("1-%1.match").arg(id));
    }

    QDir().rmpath(sessionDir);

    sessionDir.clear();

    ui->threadsTable->setRowCount(0);
}

bool LogDiff::initSession()
{
    struct timeval tv;
    gettimeofday(&tv);

    sessionDir = QString().sprintf("logdiff-%d%03d", tv.tv_sec, tv.tv_usec / 1000);

    if (!QDir().mkpath(sessionDir)) {
        error("Session error", QString("Error creating dir %1").arg(sessionDir));
        return false;
    }

    ids1.clear();
    ids2.clear();

    return true;
}

bool LogDiff::splitThreads(int logNo, QStringList &ids, QHash<QString, int> &lineNums)
{
    QString logFname = logNo ? ui->log2Edit->text() : ui->log1Edit->text();
    QFile logFile(logFname);
    if (!logFile.open(QFile::ReadOnly)) {
        error("Load error", QString("Error opening %1").arg(logFname));
        return false;
    }

    QHash<QString, QFile*> threadFiles;
    QHash<QString, QFile*> matchFiles;

    bool ret = true;

    QRegExp numbers("0x[0-9a-f]+|[0-9][0-9]+|[0-9a-z]+-[0-9a-z]+-[0-9a-z]+-[0-9a-z]+-[0-9a-z]+|[0-9]+:[0-9]+:[0-9]+| PM| AM", Qt::CaseInsensitive);

    for (;;) {
        QByteArray line = logFile.readLine(1024);
        if (line.isEmpty()) break;

        QList<QByteArray> fields = line.split(',');
        QString pid = fields[2].mid(1, fields[2].size()-2);
        QString tid = fields[3].mid(1, fields[3].size()-2);

        if (pid.isEmpty() || tid.isEmpty())
            continue;

        QString id = QString("%1-%2").arg(pid).arg(tid);

        QFile *threadFile=NULL;
        QFile *matchFile=NULL;

        if (threadFiles.contains(id)) {
            threadFile = threadFiles[id];
            matchFile = matchFiles[id];
        } else {
            QString threadFname = QDir(sessionDir).filePath(QString("%1-%2.csv").arg(logNo).arg(id));
            QString matchFname  = QDir(sessionDir).filePath(QString("%1-%2.match").arg(logNo).arg(id));

            threadFile = new QFile(threadFname);
            matchFile  = new QFile(matchFname);

            bool ret1 = threadFile->open(QFile::WriteOnly);
            bool ret2 = ret1 && matchFile->open(QFile::WriteOnly);

            if (! (ret1 && ret2)) {
                threadFile->close();
                matchFile->close();
                delete threadFile;
                delete matchFile;
                error("Split error", QString("Error creating %1").arg(ret1 ? matchFname : threadFname));
                ret = false;
                break;
            }

            ids.append(id);
            threadFiles[id] = threadFile;
            matchFiles[id] = matchFile;
            lineNums[id] = 0;
        }

        QString matchLine = line;
        matchLine.replace(numbers, "x");

        threadFile->write(line);
        matchFile->write(matchLine.toAscii().data());

        lineNums[id]++;
    }        

    foreach (QFile *file, threadFiles)
        delete file;
    foreach (QFile *file, matchFiles)
        delete file;

    return ret;
}

bool LogDiff::matchThreads(
        const QStringList &ids1,
        const QStringList &ids2,
        const QHash<QString, int> &lineNums1,
        QList<Match> &matchLst)
{
    QList<Match> matches;

    foreach (QString id1, ids1)
        foreach (QString id2, ids2) {
            QProcess diffProc;
            QString fname1 = QDir(sessionDir).filePath(QString("0-%1.match").arg(id1));
            QString fname2 = QDir(sessionDir).filePath(QString("1-%1.match").arg(id2));
            printf("diff %s %s\n", fname1.toAscii().data(), fname2.toAscii().data());

            diffProc.start("diff", QStringList() << "-du" << fname1 << fname2);
            if (!diffProc.waitForFinished() || diffProc.exitCode() >= 2) {
                error("Diff error", QString("Could not diff %1 and %2").arg(fname1).arg(fname2));
                return false;
            }

            QString hdr1 = diffProc.readLine(1024);
            QString hdr2 = diffProc.readLine(1024);
            if (hdr1.isEmpty() || hdr2.isEmpty()) {
                error("Diff error", QString("Incomplete diff output for %1 and %2").arg(fname1).arg(fname2));
                return false;
            }

            int removes = 0;
            for (;;) {
                QString line = diffProc.readLine(1024);
                if (line.isEmpty())
                    break;
                if (line.startsWith("@"))
                    continue;
                if (line.startsWith("-"))
                    removes++;
            }

            double equals = lineNums1[id1] - removes;
            double total = lineNums1[id1];
            printf("equals=%lf\n", equals*100/total);
            matches.append(Match(equals*100/total, id1, id2));
        }

    QHash<QString,QString> matchSet;

    foreach (QString id1, ids1) {
        Match best(-1);
        foreach (Match match, matches)
            if (match.id1 == id1)
                if (best.similarity < match.similarity)
                    best = match;

        matchSet[id1] = best.id2;
        matchLst.append(best);
    }

    foreach (QString id2, ids2) {
        Match best(-1);
        foreach (Match match, matches)
            if (match.id2 == id2)
                if (best.similarity < match.similarity)
                    best = match;

        if (matchSet[best.id1] != best.id2)
            matchLst.append(best);
    }

    return true;
}

bool LogDiff::getFirstLine(const QString &fname, QString &firstLine)
{
    QFile f(fname);
    if (!f.open(QFile::ReadOnly)) {
        error("Match error", QString("Could not read %1").arg(fname));
        return false;
    }

    QString line = f.readLine(1024).trimmed();

    firstLine = line;
    return true; // trim up to operation
}

void LogDiff::processLogs()
{
    if (ui->log1Edit->text().isEmpty() ||
        ui->log2Edit->text().isEmpty())
        return;

    clearSession();
    initSession();

    QHash<QString, int> lineNums1, lineNums2;

    if (!splitThreads(0, ids1, lineNums1))
        return;
    if (!splitThreads(1, ids2, lineNums2))
        return;

    QList<Match> matchLst;
    if (!matchThreads(ids1, ids2, lineNums1, matchLst))
        return;

    QTableWidget *t = ui->threadsTable;

    foreach (Match match, matchLst) {
        printf(QString("matched %1 - %2 (%3%%)\n")
                    .arg(match.id1)
                    .arg(match.id2)
                    .arg(match.similarity)
                .toAscii().data());

        int row = t->rowCount();
        t->insertRow(row);

        QStringList pidtid1 = match.id1.split("-");
        QStringList pidtid2 = match.id2.split("-");

        QString fname1 = QDir(sessionDir).filePath(QString("0-%1.csv").arg(match.id1));
        QString fname2 = QDir(sessionDir).filePath(QString("1-%1.csv").arg(match.id2));
        QString firstLine1, firstLine2;
        if (!getFirstLine(fname1, firstLine1)) return;
        if (!getFirstLine(fname2, firstLine2)) return;

        QString items[] = {
            pidtid1[0],
            pidtid1[1],
            pidtid2[0],
            pidtid2[1],
            QString::number(lineNums1[match.id1]),
            QString::number(lineNums2[match.id2]),
            QString().sprintf("%.0f%%", match.similarity),
            firstLine1,
        };

        for (int col=0; col<8; col++)
            t->setItem(row, col, new QTableWidgetItem(items[col]));
    }
}

void LogDiff::on_threadsTable_cellDoubleClicked(int row, int)
{
    QTableWidget *t = ui->threadsTable;
    QString pid1 = t->item(row, 0)->text();
    QString tid1 = t->item(row, 1)->text();
    QString pid2 = t->item(row, 2)->text();
    QString tid2 = t->item(row, 3)->text();

    QString fname1 = QDir(sessionDir).filePath(QString("0-%1-%2.csv").arg(pid1).arg(tid1));
    QString fname2 = QDir(sessionDir).filePath(QString("1-%1-%2.csv").arg(pid2).arg(tid2));

    QProcess kdiff3Proc;
    if (!kdiff3Proc.startDetached("kdiff3", QStringList() <<
            "--cs" << "Show Toolbar=0" <<
            "--cs" << "Show Statusbar=0" <<
            "--cs" << "IgnoreNumbers=1" <<
            fname1 << fname2)) {
        error("Match error", QString("Could not start kdiff3 %1 %2").arg(fname1).arg(fname2));
        return;
    }
}
