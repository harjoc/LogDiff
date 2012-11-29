#ifndef LOGDIFF_H
#define LOGDIFF_H

#include <QMainWindow>

namespace Ui {
class LogDiff;
}

struct Match {
    Match(double similarity=0, const QString &id1=QString(), const QString &id2=QString()):
        similarity(similarity), id1(id1), id2(id2) { }

    double similarity;
    QString id1;
    QString id2;
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
    bool splitThreads(int logNo, QStringList &ids, QHash<QString, int> &lineNums);
    bool matchThreads(const QStringList &ids1,
                      const QStringList &ids2,
                      const QHash<QString, int> &lineNums1,
                      QList<Match> &matchLst);
    bool getFirstLine(const QString &fname, QString &firstLine);
    void clearSession();
    bool initSession();

    // fields

    QString sessionDir;

    QStringList ids1;
    QStringList ids2;
};

#endif // LOGDIFF_H
