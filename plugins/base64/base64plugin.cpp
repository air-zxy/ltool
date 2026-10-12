// ltool 的 DLL 插件示例：Base64 编解码。
//
// 这个 demo 的目的不是"再做一个 Base64"，而是把整条链跑通一遍：
//   plugin.json（元数据）→ QPluginLoader 只读元数据 → 工具页一张卡片 → 点开才 createPage
//   → 搜索框按关键词直达（只认开头、不独占整页）→ 主题跟着主程序走。
//
// 编译要求（和 src/ltool-plugin.h 上面那段注释同一件事）：同一个 MSVC 工具集、
// 同一份 /MD 运行的 Qt6*.dll、同为 Release。凑不齐就别编插件，会崩在两边的堆上。
#include "ltool-plugin.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <QtPlugin>

namespace {

class Base64Page : public QWidget
{
public:
    explicit Base64Page(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("pluginBase64Page"));
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(10, 8, 10, 8);
        lay->setSpacing(8);

        auto* head = new QHBoxLayout;
        m_encode = new QPushButton(QStringLiteral("编码 →"), this);
        m_decode = new QPushButton(QStringLiteral("← 解码"), this);
        m_copy = new QPushButton(QStringLiteral("复制结果"), this);
        head->addWidget(m_encode);
        head->addWidget(m_decode);
        head->addStretch(1);
        head->addWidget(m_copy);
        lay->addLayout(head);

        m_in = new QPlainTextEdit(this);
        m_in->setPlaceholderText(QStringLiteral("把文本或 base64 粘到这里"));
        m_out = new QPlainTextEdit(this);
        m_out->setReadOnly(true);
        m_out->setPlaceholderText(QStringLiteral("结果"));
        m_status = new QLabel(QStringLiteral("等着输入"), this);
        m_status->setObjectName(QStringLiteral("muted"));
        lay->addWidget(new QLabel(QStringLiteral("输入"), this));
        lay->addWidget(m_in, 1);
        lay->addWidget(new QLabel(QStringLiteral("结果"), this));
        lay->addWidget(m_out, 1);
        lay->addWidget(m_status);

        connect(m_encode, &QPushButton::clicked, this, [this] { run(true); });
        connect(m_decode, &QPushButton::clicked, this, [this] { run(false); });
        connect(m_copy, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(m_out->toPlainText());
            m_status->setText(QStringLiteral("已复制 %1 个字符").arg(m_out->toPlainText().size()));
        });
    }

private:
    void run(bool encode)
    {
        const QByteArray raw = m_in->toPlainText().toUtf8();
        if (encode) {
            m_out->setPlainText(QString::fromLatin1(raw.toBase64()));
            m_status->setText(QStringLiteral("编码：%1 字节 → %2 个字符")
                                  .arg(raw.size()).arg(m_out->toPlainText().size()));
            return;
        }
        // 严格模式：解不出合法 Base64 就别硬解，给十六进制让人看清到底是什么
        const QByteArray out = QByteArray::fromBase64(m_in->toPlainText().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        if (out.isEmpty() && !m_in->toPlainText().trimmed().isEmpty()) {
            const QByteArray bytes = m_in->toPlainText().toUtf8();
            m_out->setPlainText(QString::fromLatin1(bytes.toHex(' ').toUpper()));
            m_status->setText(QStringLiteral("不是合法 base64，已按原字节给十六进制（%1 字节）").arg(bytes.size()));
            return;
        }
        m_out->setPlainText(QString::fromUtf8(out));
        m_status->setText(QStringLiteral("解码：%1 个字符 → %2 字节")
                              .arg(m_in->toPlainText().size()).arg(out.size()));
    }

    QPlainTextEdit* m_in = nullptr;
    QPlainTextEdit* m_out = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_encode = nullptr;
    QPushButton* m_decode = nullptr;
    QPushButton* m_copy = nullptr;
};

} // namespace

class Base64Plugin : public QObject, public LToolPlugin
{
    Q_OBJECT
    Q_INTERFACES(LToolPlugin)
    Q_PLUGIN_METADATA(IID LTOOL_PLUGIN_IID FILE "plugin.json")

public:
    int apiVersion() const override { return LTOOL_PLUGIN_API_VERSION; }
    QString id() const override { return QStringLiteral("base64"); }
    QString name() const override { return QStringLiteral("Base64（插件版）"); }
    QString desc() const override { return QStringLiteral("DLL 插件示例：编码/解码，解不出给十六进制"); }
    QStringList keywords() const override
    {
        return { QStringLiteral("base64"), QStringLiteral("b64"), QStringLiteral("编解码") };
    }
    QWidget* createPage(QWidget* parent) override { return new Base64Page(parent); }
};

#include "base64plugin.moc"
