#include "logdiff.h"
#include "ui_logdiff.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QThreadPool>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

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
            "Similar" << "First line";

    int widths[] = {40, 40, 40, 40, 45, 45, 48, 48, 30};

    t->setColumnCount(cols.size());
    t->setHorizontalHeaderLabels(cols);

    for (int i=0; i<cols.size(); i++) {
         t->horizontalHeaderItem(i)->setTextAlignment(Qt::AlignLeft);
         t->setColumnWidth(i, widths[i]);
    }

    #if 0
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
    ui->log2Edit->clear();
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
    ui->log2Edit->clear();
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

    sessionDir = QDir::tempPath() + QString().sprintf("/logdiff-%d%03d", tv.tv_sec, tv.tv_usec / 1000);

    if (!QDir().mkpath(sessionDir)) {
        error("Session error", QString("Error creating dir %1").arg(sessionDir));
        return false;
    }

    ids1.clear();
    ids2.clear();

    pidCol = -1;
    tidCol = -1;
    operCol = -1;

    return true;
}

void removeBom(QByteArray &line)
{
    int endOfBom = 0;
    while (endOfBom<line.size() && 127 < (unsigned char)line.at(endOfBom))
        endOfBom++;
    if (endOfBom)
        line.remove(0, endOfBom);
}

bool LogDiff::splitThreads(int logNo, QStringList &ids, QHash<QString, int> &lineNums, bool &slow)
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

    QProgressDialog progress;
    progress.setLabelText(QString("Splitting log file #%1 ...").arg(logNo+1));
    progress.setMinimum(0);
    progress.setMaximum(100);
    progress.setMinimumDuration(250);
    progress.setValue(0);

    if (slow) {
        progress.show();
        QApplication::processEvents();
    }

    bool firstLine = true;

    for (int counter=0; ; counter++) {
        if (counter % 2000 == 0)
            QApplication::processEvents();

        QByteArray line = logFile.readLine(1024);
        if (firstLine) {
            firstLine = false;

            // "Time of Day","Process Name","PID","Operation","Path","Result","Detail","TID"

            int pidPos  = line.indexOf("\"PID\"");
            int tidPos  = line.indexOf("\"TID\"");
            int operPos = line.indexOf("\"Operation\"");
            if (pidPos < 0 || tidPos < 0 || operPos < 0) {
                error("Load error", QString("PID, TID, and Operation fields not present in %1").arg(logFname));
                ret = false;
                break;
            }

            int newPidCol  = line.left(pidPos).count(',');
            int newTidCol  = line.left(tidPos).count(',');
            int newOperCol = line.left(operPos).count(',');

            if (pidCol < 0) {
                pidCol  = newPidCol;
                tidCol  = newTidCol;
                operCol = newOperCol;
            } else if (pidCol != newPidCol || tidCol != newTidCol || operCol != newOperCol) {
                error("Load error", QString("Positions of PID, TID and Operation fields differ (%1,%2,%3 vs %4,%5,%6)")
                      .arg(pidCol).arg(tidCol).arg(operCol).arg(newPidCol).arg(newTidCol).arg(newOperCol));
                ret = false;
                break;
            }

            continue;
        }

        if (line.isEmpty()) break;

        QList<QByteArray> fields = line.split(',');
        if (fields.size() < 5)
            continue;

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

    slow |= progress.isVisible();
    return ret;
}

void DiffTask::postError(const QString &error)
{
    QApplication::postEvent(parent, new ThreadErrorEvent(error));
}

void DiffTask::run()
{
    QStringList fnameList2;
    foreach (QString id2, ids2)
        fnameList2.append(QString("1-%1.match").arg(id2)); // we start diff in sessionDir

    QString fname1 = QString("0-%1.match").arg(id1); // we start diff in sessionDir

    QProcess diffProc;

    QStringList args;
    args << "-d";
    args << "-u";
    args << "--from-file" << fname1;
    args.append(fnameList2);

    diffProc.setWorkingDirectory(sessionDir);
    diffProc.start("diff", args);

    if (!diffProc.waitForFinished() || diffProc.exitCode() >= 2) {
        postError(QString("Could not compare %1 to log #2").arg(fname1));
        return;
    }

    int i2=0;

    QString hdr1 = diffProc.readLine(1024);

    QList<Match> *matches = new QList<Match>();

    for (;;) {
        // hdr1 is also left for us by at the end of this loop below

        QString hdr2 = diffProc.readLine(1024);

        if (hdr1.isEmpty() ^ hdr2.isEmpty()) {
            postError(QString("Incomplete diff output for %1").arg(fname1));
            delete matches;
            return;
        }

        if (hdr1.isEmpty())
            break;

        if (!hdr1.startsWith("--- 0-") || !hdr2.startsWith("+++ 1-")) {
            postError(QString("Expecting ---/+++ and prefixes in diff output for %1").arg(fname1));
            delete matches;
            return;
        }

        int sp1 = hdr1.indexOf('\t', 6);
        int sp2 = hdr2.indexOf('\t', 6);
        if (sp1 < 0 || sp2 < 0) {
            postError(QString("Could not get name from diff output for %1").arg(fname1));
            delete matches;
            return;
        }

        QString name1 = hdr1.mid(6, sp1-6);
        QString name2 = hdr2.mid(6, sp2-6);

        if (!name1.endsWith(".match") || !name2.endsWith(".match")) {
            postError(QString("Unexpected names from diff output: %1 and %2").arg(name1).arg(name2));
            delete matches;
            return;
        }

        name1.truncate(name1.size()-6);
        name2.truncate(name2.size()-6);

        if (name1 != id1) {
            postError(QString("Unexpected 'from' file in diff output for %1: %2").arg(fname1).arg(name1));
            delete matches;
            return;
        }

        QString id2;

        // diff doesn't output anything for identical files
        while (i2 < ids2.size()) {
            id2 = ids2.at(i2);
            if (id2 == name2) break;

            matches->append(Match(0, 0, id1, id2));
            i2++;
        }

        if (i2 == ids2.size()) {
            postError(QString("Unexpected 'to' file in diff output for %1: %2").arg(fname1).arg(name2));
            delete matches;
            return;
        }

        // done with the header, now count the removed lines

        // by assuming that no log line can begin with "--- 0-" we can
        // read the diff lines as context-independent, and avoid looking
        // at the @@ lines and the gross parsing.

        int removals = 0;
        int additions = 0;
        for (;;) {
            QString line = diffProc.readLine(1024);
            if (line.isEmpty() || line.startsWith("--- 0-")) {
                hdr1 = line;
                break;
            }

            if (line.startsWith("-"))
                removals++;
            if (line.startsWith("+"))
                additions++;
        }

        matches->append(Match(removals, additions,
                id1, id2));

        i2++;
    }

    // end of diff output, so the rest of the files are identical
    while (i2 < ids2.size()) {
        QString id2 = ids2.at(i2);
        matches->append(Match(0, 0, id1, id2));
        i2++;
    }

    QApplication::postEvent(parent, new ThreadMatchEvent(matches));
    // the gui thread will delete matches
}

void LogDiff::customEvent(QEvent *event)
{
    switch (event->type()) {
        case ThreadMatchEventType:
        {
            ThreadMatchEvent *mevent = (ThreadMatchEvent *)event;

            matches.append(*mevent->matches);
            delete mevent->matches;

            if (diffsFailed)
                break;

            matchProgress.setValue(++diffsDone);
            if (diffsDone == ids1.size())
                selectMatches();

            break;
        }

        case ThreadErrorEventType:
        {
            ThreadErrorEvent *eevent = (ThreadErrorEvent *)event;
            error("Diff error", eevent->error);
            diffsFailed = true;
            break;
        }

        default:
            QMainWindow::customEvent(event);
    }
}

void LogDiff::selectMatches()
{
    if (matches.size() != ids1.size() * ids2.size()) {
        error("Diff error", QString("Only collected %1 results out of %2")
              .arg(matches.size()).arg(ids1.size() * ids2.size()));
        return;
    }

    QHash<QString,QString> matchSet;

    bestMatches.clear();
    otherMatches.clear();

    foreach (QString id1, ids1) {
        Match best;
        foreach (Match match, matches)
            if (match.id1 == id1)
                if (best.removals < 0 || lineNums1[best.id1]-best.removals < lineNums1[match.id1]-match.removals)
                    best = match;

        matchSet[id1] = best.id2;
        bestMatches.append(best);
    }

    foreach (QString id2, ids2) {
        Match best;
        foreach (Match match, matches)
            if (match.id2 == id2)
                if (best.removals < 0 || lineNums2[best.id2]-best.additions < lineNums2[match.id2]-match.additions)
                    best = match;

        if (matchSet[best.id1] != best.id2)
            otherMatches.append(best);
    }

    QHash<quint64, QString> empty;
    addMatches(bestMatches, otherMatches, empty, empty);
}

quint64 stridToIntid(const QString &id)
{
    QByteArray idba = id.toAscii();
    const char *data = idba.data();
    quint64 pid = atoi(data);

    const char *dash = strchr(data, '-');
    quint64 tid = dash ? atoi(dash+1) : 0;

    return tid | (pid << 32);
}

void LogDiff::addMatches(const QList<Match> &best, const QList<Match> &other, const QHash<quint64, QString> firstLines1, const QHash<quint64, QString> firstLines2)
{
    ui->threadsTable->setRowCount(0);

    foreach (Match match, best) {
        QString fline1 = firstLines1[stridToIntid(match.id1)];
        QString fline2 = firstLines2[stridToIntid(match.id2)];
        QString fline = !fline1.isEmpty() ? fline1 : (!fline2.isEmpty() ? fline2 : QString());

        if (!addMatch(match, fline)) {
            ui->threadsTable->setRowCount(0);
            return;
        }
    }

    if (!other.isEmpty()) {
        QTableWidget *t = ui->threadsTable;
        int row = t->rowCount();
        t->insertRow(row);
        t->setItem(row, t->columnCount()-1, new QTableWidgetItem("--- Other possible matches ---"));
    }

    foreach (Match match, other) {
        QString fline1 = firstLines1[stridToIntid(match.id1)];
        QString fline2 = firstLines2[stridToIntid(match.id2)];
        QString fline = !fline1.isEmpty() ? fline1 : (!fline2.isEmpty() ? fline2 : QString());

        if (!addMatch(match, fline)) {
            ui->threadsTable->setRowCount(0);
            return;
        }
    }
}

void LogDiff::matchThreads(bool &slow)
{
    matches.clear();
    diffsDone = 0;
    diffsFailed = false;

    matchProgress.setLabelText(QString("Matching %1*%2 threads ...").arg(ids1.size()).arg(ids2.size()));
    matchProgress.setCancelButton(NULL);
    matchProgress.setMinimum(0);
    matchProgress.setMaximum(ids1.size());
    matchProgress.setMinimumDuration(250);
    matchProgress.setValue(0);    

    if (slow) {
        matchProgress.show();
        QApplication::processEvents();
    }

    foreach (QString id1, ids1)
        QThreadPool::globalInstance()->start(new DiffTask(this, sessionDir, id1, ids2));
}

QString LogDiff::trimFirstLine(const QString &line)
{
    int comma=0;
    for (int i=0; i<operCol; i++) {
        int ncomma = line.indexOf(',', comma);
        if (ncomma<0) { comma=0; break; }
        comma = ncomma+1;
    }

    return line.right(line.size()-comma);
}

bool LogDiff::getFirstLine(const QString &fname, QString &firstLine)
{
    QFile f(fname);
    if (!f.open(QFile::ReadOnly)) {
        error("Match error", QString("Could not read %1").arg(fname));
        return false;
    }

    firstLine = f.readLine(1024).trimmed();
    return true;
}

bool LogDiff::addMatch(const Match &match, const QString &firstLine)
{
    QTableWidget *t = ui->threadsTable;
    int row = t->rowCount();
    t->insertRow(row);

    QStringList pidtid1 = match.id1.split("-");
    QStringList pidtid2 = match.id2.split("-");

    QString fname1 = QDir(sessionDir).filePath(QString("0-%1.csv").arg(match.id1));
    QString line;

    if (!firstLine.isEmpty()) {
        line = firstLine;
    } else {
        if (!getFirstLine(fname1, line)) return false;
    }

    double similarity = lineNums1[match.id1] - match.removals;
    similarity /= lineNums1[match.id1];    

    QString items[] = {
        pidtid1[0],
        pidtid2[0],
        pidtid1[1],
        pidtid2[1],
        QString::number(lineNums1[match.id1]),
        QString::number(lineNums2[match.id2]),
        QString().sprintf("%.0f%%", similarity*100),
        trimFirstLine(line),
    };
    for (int col=0; col<8; col++)
        t->setItem(row, col, new QTableWidgetItem(items[col]));

    return true;
}

void LogDiff::processLogs()
{
    if (ui->log1Edit->text().isEmpty() ||
        ui->log2Edit->text().isEmpty())
        return;

    clearSession();
    initSession();

    bool slow = false;

    if (!splitThreads(0, ids1, lineNums1, slow))
        return;
    if (!splitThreads(1, ids2, lineNums2, slow))
        return;

    matchThreads(slow);
}

void LogDiff::on_threadsTable_cellDoubleClicked(int row, int)
{
    QString extn = ui->ignoreNumbersCheck->isChecked() ? "match" : "csv";

    QTableWidget *t = ui->threadsTable;
    QString pid1 = t->item(row, 0)->text();
    QString pid2 = t->item(row, 1)->text();
    QString tid1 = t->item(row, 2)->text();
    QString tid2 = t->item(row, 3)->text();

    QString fname1 = QDir(sessionDir).filePath(QString("0-%1-%2.%3").arg(pid1).arg(tid1).arg(extn));
    QString fname2 = QDir(sessionDir).filePath(QString("1-%1-%2.%3").arg(pid2).arg(tid2).arg(extn));

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

bool LogDiff::grepFile(const QString &fname, const QString &text, QHash<quint64, QString> &matches)
{
    QStringList args;
    if (ui->regexpCheck->isChecked())
        args << "-E";
    args << text
         << fname;

    QProcess grepProc;
    grepProc.start("grep", args);

    if (!grepProc.waitForFinished() || grepProc.exitCode() >= 2) {
        error("Search error", QString("Could not run grep on %1").arg(fname));
        return false;
    }

    for (;;) {
        char line[1024];
        qint64 len = grepProc.readLine(line, sizeof(line));
        if (len <= 0) break;

        quint64 pid, tid;

        const char *ptr = line;

        int colMin = min(pidCol, tidCol);
        int colMax = max(pidCol, tidCol);

        for (int i=0; i<colMin; i++) {
            ptr = strchr(ptr, ',');
            if (ptr) ptr++; else break;
        }

        ((pidCol < tidCol) ? pid : tid) = atoi(ptr+1);

        for (int i=colMin; i<colMax; i++) {
            ptr = strchr(ptr, ',');
            if (ptr) ptr++; else break;
        }

        ((pidCol < tidCol) ? tid : pid) = atoi(ptr+1);

        quint64 id = tid | (pid << 32);
        if (!matches.contains(id))
            matches[id] = QByteArray(line, len).trimmed();
    }

    return true;
}


void LogDiff::on_searchBtn_clicked()
{
    const QString &text = ui->searchEdit->text();

    QHash<quint64, QString> lines1;
    QHash<quint64, QString> lines2;

    if (!text.isEmpty()) {
        if (!grepFile(ui->log1Edit->text(), text, lines1))
            return;
        if (!grepFile(ui->log2Edit->text(), text, lines2))
            return;
    }

    QList<Match> bestMatchesFiltered;
    QList<Match> otherMatchesFiltered;

    foreach (Match match, bestMatches) {
        quint64 nid1 = stridToIntid(match.id1);
        quint64 nid2 = stridToIntid(match.id2);
        if (lines1.contains(nid1) || lines2.contains(nid2))
            bestMatchesFiltered.append(match);
    }

    foreach (Match match, otherMatches) {
        quint64 nid1 = stridToIntid(match.id1);
        quint64 nid2 = stridToIntid(match.id2);
        if (lines1.contains(nid1) || lines2.contains(nid2))
            otherMatchesFiltered.append(match);
    }

    addMatches(bestMatchesFiltered, otherMatchesFiltered, lines1, lines2);
}

void LogDiff::on_searchEdit_returnPressed()
{
    on_searchBtn_clicked();
}
