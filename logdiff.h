#ifndef LOGDIFF_H
#define LOGDIFF_H

#include <QMainWindow>

#include <QThread>
#include <QProgressDialog>
#include <QRunnable>
#include <QHash>
#include <QEvent>

namespace Ui {
class LogDiff;
}

struct Match {
    Match(int removals=-1, int additions=-1, const QString &id1=QString(), const QString &id2=QString()):
        removals(removals),
        additions(additions),
        id1(id1),
        id2(id2) { }

    int removals;
    int additions;
    QString id1;
    QString id2;

    /*double similarity() const {
        double s = lines1 - removals;
        return s / (double)lines1;
    }*/
};

class DiffTask: public QRunnable
{
public:
    DiffTask(QObject *parent, const QString &sessionDir, const QString &id1, const QStringList &ids2):
        QRunnable(),
        parent(parent),
        sessionDir(sessionDir), id1(id1), ids2(ids2) { }

    void run();

private:
    void postError(const QString &error);

    QObject *parent;
    QString sessionDir;
    QString id1;
    QStringList ids2;
};

const QEvent::Type ThreadMatchEventType = (QEvent::Type)9493;
const QEvent::Type ThreadErrorEventType = (QEvent::Type)9494;

class ThreadMatchEvent: public QEvent {
public:
    ThreadMatchEvent(QList<Match> *matches):
        QEvent(ThreadMatchEventType),
        matches(matches) { }

    QList<Match> *matches;
};

class ThreadErrorEvent: public QEvent {
public:
    ThreadErrorEvent(const QString &error):
        QEvent(ThreadErrorEventType),
        error(error) { }

    QString error;
};

class LogDiff : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit LogDiff(QWidget *parent = 0);
    ~LogDiff();

private slots:
    void on_browse1Btn_clicked();
    void on_browse2Btn_clicked();
    void on_log1Edit_returnPressed();
    void on_log2Edit_returnPressed();

    void on_threadsTable_cellDoubleClicked(int row, int);

    void on_searchEdit_returnPressed();

    void on_searchBtn_clicked();

private:
    Ui::LogDiff *ui;

    void clearSession();
    bool initSession();
    void error(const QString &title, const QString &text);

    void processLogs();
    bool splitThreads(int logNo, QStringList &ids, QHash<QString, int> &lineNums, bool &slow);
    void customEvent(QEvent *event);
    void matchThreads(bool &slow);
    void selectMatches();

    bool getFirstLine(const QString &fname, QString &firstLine);
    QString trimFirstLine(const QString &line);
    void addMatches(const QList<Match> &best, const QList<Match> &other,
            const QHash<quint64, QString> firstLines1, const QHash<quint64, QString> firstLines2);
    bool addMatch(const Match &match, const QString &firstLine);

    bool grepFile(const QString &fname, const QString &text, QHash<quint64, QString> &matches);

    // fields

    QString sessionDir;

    int fieldsNum;
    int pidCol;
    int tidCol;
    int operCol;

    QStringList ids1;
    QStringList ids2;

    QHash<QString, int> lineNums1;
    QHash<QString, int> lineNums2;

    int diffsDone;
    bool diffsFailed;

    QList<Match> matches;
    QList<Match> bestMatches;
    QList<Match> otherMatches;
    QProgressDialog matchProgress;

};

#endif // LOGDIFF_H
