#ifndef LOGDIFF_H
#define LOGDIFF_H

#include <QMainWindow>

namespace Ui {
class LogDiff;
}

struct Match {
    Match(int removals=-1, int additions=-1, int lines1=-1, int lines2=-1, const QString &id1=QString(), const QString &id2=QString()):
        removals(removals),
        additions(additions),
        lines1(lines1),
        lines2(lines2),
        id1(id1),
        id2(id2) { }

    int removals;
    int additions;
    int lines1;
    int lines2;
    QString id1;
    QString id2;

    double similarity() const {
        double s = lines1 - removals;
        return s / (double)lines1;
    }

    double revSimilarity() const {
        double s = lines2 - additions;
        return s * 100 / (double)lines2;
    }
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

private:
    Ui::LogDiff *ui;

    void error(const QString &title, const QString &text);
    void processLogs();
    bool splitThreads(int logNo, QStringList &ids, QHash<QString, int> &lineNums, bool &slow);
    bool matchThreads(const QStringList &ids1,
                      const QStringList &ids2,
                      const QHash<QString, int> &lineNums1,
                      const QHash<QString, int> &lineNums2,
                      QList<Match> &bestMatches,
                      QList<Match> &otherMatches,
                      bool &slow);
    bool getFirstLine(const QString &fname, QString &firstLine);
    void addMatch(const Match &match,
                  QHash<QString, int> lineNums1,
                  QHash<QString, int> lineNums2);
    void clearSession();
    bool initSession();

    // fields

    QString sessionDir;

    QStringList ids1;
    QStringList ids2;
};

#endif // LOGDIFF_H
