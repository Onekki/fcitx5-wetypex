#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLockFile>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

static QString stateDir() {
  const auto override = qEnvironmentVariable("WETYPE_STATE_DIR");
  return override.isEmpty() ? QStandardPaths::writableLocation(
                                  QStandardPaths::GenericDataLocation) +
                                  "/fcitx5-wetypex/state"
                            : override;
}
static QString candidateIcon(const QString &name) {
  return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                "fcitx5-wetypex/ui/candidate-icons/" + name +
                                    ".svg");
}
static bool sendAction(quint64 session, quint64 epoch, const QString &token,
                       const QString &action, const QString &text = {}) {
  QDir().mkpath(stateDir());
  QSaveFile file(stateDir() + "/vmode-action.json");
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QJsonObject value{{"version", QDateTime::currentMSecsSinceEpoch()},
                    {"session", static_cast<qint64>(session)},
                    {"epoch", static_cast<qint64>(epoch)},
                    {"token", token},
                    {"action", action},
                    {"text", text}};
  const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
  if (file.write(bytes) != bytes.size())
    return false;
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return file.commit();
}
class VModeWindow : public QWidget {
  quint64 session_, epoch_;
  QString token_;
  bool actionSent_ = false;

public:
  VModeWindow(quint64 session, quint64 epoch, QString token)
      : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                             Qt::WindowStaysOnTopHint |
                             Qt::WindowDoesNotAcceptFocus),
        session_(session), epoch_(epoch), token_(std::move(token)) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
  }
  void finish(const QString &action, const QString &text = {}) {
    if (sendAction(session_, epoch_, token_, action, text)) {
      actionSent_ = true;
      close();
    }
  }

protected:
  void closeEvent(QCloseEvent *event) override {
    if (!actionSent_)
      sendAction(session_, epoch_, token_, "cancel");
    QWidget::closeEvent(event);
    // Tool windows do not necessarily trigger quitOnLastWindowClosed.
    QCoreApplication::quit();
  }
};
static bool parseNonZero(const QString &text, quint64 *value) {
  bool ok = false;
  const auto parsed = text.toULongLong(&ok);
  if (!ok || parsed == 0)
    return false;
  *value = parsed;
  return true;
}
static bool parseCoordinate(const QString &text, int *value) {
  bool ok = false;
  const auto parsed = text.toInt(&ok);
  if (!ok)
    return false;
  *value = parsed;
  return true;
}
int main(int argc, char **argv) {
  const QString usage = QStringLiteral(
      "用法：fcitx5-wetypex-vmode symbols <session> <x> <y> <epoch> <token>");
  const QString tool = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
  quint64 session = 0, epoch = 0;
  int x = 0, y = 0;
  const QString token = argc > 6 ? QString::fromLocal8Bit(argv[6]) : QString();
  if (argc != 7 || tool != "symbols" ||
      !parseNonZero(QString::fromLocal8Bit(argv[2]), &session) ||
      !parseCoordinate(QString::fromLocal8Bit(argv[3]), &x) ||
      !parseCoordinate(QString::fromLocal8Bit(argv[4]), &y) ||
      !parseNonZero(QString::fromLocal8Bit(argv[5]), &epoch) ||
      token.isEmpty()) {
    std::fprintf(stderr, "%s\n", usage.toLocal8Bit().constData());
    return 2;
  }
  // XWayland honors the no-focus window hint; xdg_toplevel does not.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
      !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
      !qEnvironmentVariableIsEmpty("DISPLAY"))
    qputenv("QT_QPA_PLATFORM", "xcb");
  QApplication app(argc, argv);
  app.setApplicationName("fcitx5-wetypex-vmode");
  QDir().mkpath(stateDir());
  QLockFile lock(stateDir() + "/vmode.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock())
    return 0;
  const QString title = QStringLiteral("符号");
  VModeWindow window(session, epoch, token);
  window.setWindowTitle("WeTypeX " + title);
  window.setStyleSheet(
      "QWidget{background:#f7f7f7;color:#202124;font:14px 'Noto Sans CJK SC';}"
      "QPushButton{background:white;border:1px solid "
      "#e7e7e7;border-radius:8px;padding:6px;}"
      "QPushButton:hover{border-color:#23c891;background:#f2fffa;}"
      "QTabWidget::pane{border:0;} QTabBar::tab:selected{color:#23c891;}");
  auto *outer = new QVBoxLayout(&window);
  outer->setContentsMargins(8, 8, 8, 8);
  outer->setSpacing(6);
  auto *header = new QHBoxLayout;
  header->addWidget(new QLabel(title));
  header->addStretch();
  auto *close = new QPushButton("关闭");
  close->setFocusPolicy(Qt::NoFocus);
  QObject::connect(close, &QPushButton::clicked, &window, &QWidget::close);
  header->addWidget(close);
  outer->addLayout(header);
  auto *body = new QWidget;
  auto *root = new QVBoxLayout(body);
  root->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(body);
  auto showEmpty = [&](const QString &text) {
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    root->addWidget(label);
    root->addStretch();
  };
  QFile file(QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                    "fcitx5-wetypex/wetypex-symbols.json"));
  const auto groups = file.open(QIODevice::ReadOnly)
                          ? QJsonDocument::fromJson(file.readAll()).array()
                          : QJsonArray();
  if (groups.isEmpty()) {
    showEmpty("未找到可用符号表");
  } else {
    auto *tabs = new QTabWidget;
    tabs->setFocusPolicy(Qt::NoFocus);
    tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    for (const auto &groupValue : groups) {
      auto group = groupValue.toObject();
      auto *tabBody = new QWidget;
      auto *grid = new QGridLayout(tabBody);
      int n = 0;
      for (const auto &subValue : group.value("groupData").toArray())
        for (const auto &rowValue :
             subValue.toObject().value("subGroupData").toArray())
          for (const auto &symbolValue : rowValue.toArray()) {
            QString text = symbolValue.toString();
            auto *button = new QPushButton(text);
            button->setFocusPolicy(Qt::NoFocus);
            button->setFixedSize(38, 32);
            QObject::connect(button, &QPushButton::clicked, &window,
                             [&window, text] { window.finish("commit", text); });
            grid->addWidget(button, n / 9, n % 9);
            ++n;
          }
      auto *scroll = new QScrollArea;
      scroll->setFocusPolicy(Qt::NoFocus);
      scroll->setWidgetResizable(true);
      scroll->setWidget(tabBody);
      const auto iconName =
          QFileInfo(group.value("iconPath").toString()).baseName();
      tabs->addTab(scroll, QIcon(candidateIcon(iconName)),
                   group.value("groupName").toString());
    }
    root->addWidget(tabs);
  }
  window.resize(450, 360);
  window.move(x, y);
  window.show();
  return app.exec();
}
