#include "DiskDialogs.h"

#include "util/Format.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWizardPage>

#include <limits>

namespace {
constexpr quint64 MiB = 1024ULL * 1024ULL;

QLabel *wizardHeading(const QString &text)
{
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setPointSize(font.pointSize() + 2);
    font.setBold(true);
    label->setFont(font);
    return label;
}

QWizardPage *simplePage(const QString &title, const QStringList &paragraphs)
{
    auto *page = new QWizardPage;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(wizardHeading(title));
    for (const QString &paragraph : paragraphs) {
        auto *label = new QLabel(paragraph);
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    layout->addStretch();
    return page;
}

QPixmap wizardWatermark()
{
    QPixmap image(150, 360);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, image.width(), image.height());
    gradient.setColorAt(0, QColor("#0b5d68"));
    gradient.setColorAt(0.55, QColor("#0b747c"));
    gradient.setColorAt(1, QColor("#094855"));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QPen(QColor(255, 255, 255, 45), 2));
    painter.drawArc(QRect(-50, 55, 240, 210), 15 * 16, 125 * 16);
    painter.drawArc(QRect(-15, 115, 245, 205), 35 * 16, 125 * 16);
    return image;
}

void configureWizard(QWizard *wizard)
{
    wizard->setWizardStyle(QWizard::ClassicStyle);
    wizard->setOption(QWizard::NoBackButtonOnStartPage);
    wizard->setPixmap(QWizard::WatermarkPixmap, wizardWatermark());
    wizard->setButtonText(QWizard::BackButton, "< Back");
    wizard->setButtonText(QWizard::NextButton, "Next >");
    wizard->setButtonText(QWizard::FinishButton, "Finish");
    wizard->resize(620, 430);
}

QString formatName(const QString &type, const QList<FileSystemCapability> &capabilities)
{
    for (const auto &capability : capabilities)
        if (capability.type == type) return capability.displayName;
    return type;
}

void populateFileSystems(QComboBox *combo, const QList<FileSystemCapability> &capabilities,
                         const QString &preferred = {})
{
    for (const FileSystemCapability &capability : capabilities) {
        if (!capability.canFormat) continue;
        combo->addItem(capability.displayName, capability.type);
    }
    const int preferredIndex = combo->findData(preferred);
    if (preferredIndex >= 0) combo->setCurrentIndex(preferredIndex);
}
}

InitializeDiskDialog::InitializeDiskDialog(const QList<QPair<int, BlockDevice>> &disks,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Initialize Disk");
    setModal(true);
    resize(470, 350);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("You must initialize a disk before Logical Disk Manager can access it."));
    layout->addSpacing(8);
    layout->addWidget(new QLabel("Select disks:"));
    auto *diskBox = new QGroupBox;
    auto *diskLayout = new QVBoxLayout(diskBox);
    for (const auto &[index, disk] : disks) {
        auto *check = new QCheckBox(QString("Disk %1    %2").arg(index).arg(Format::bytes(disk.size)));
        check->setChecked(true);
        diskLayout->addWidget(check);
        m_diskChecks << qMakePair(index, check);
    }
    layout->addWidget(diskBox);
    auto *style = new QGroupBox("Use the following partition style for the selected disks:");
    auto *styleLayout = new QVBoxLayout(style);
    m_mbr = new QRadioButton("MBR (Master Boot Record)");
    m_gpt = new QRadioButton("GPT (GUID Partition Table)");
    m_mbr->setChecked(true);
    styleLayout->addWidget(m_mbr);
    styleLayout->addWidget(m_gpt);
    layout->addWidget(style);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!selectedDiskIndexes().isEmpty()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QList<int> InitializeDiskDialog::selectedDiskIndexes() const
{
    QList<int> result;
    for (const auto &[index, check] : m_diskChecks)
        if (check->isChecked()) result << index;
    return result;
}

QString InitializeDiskDialog::partitionTableType() const
{
    return m_gpt->isChecked() ? "gpt" : "dos";
}

NewSimpleVolumeWizard::NewSimpleVolumeWizard(
    const DiskFreeRegion &region, const QList<FileSystemCapability> &capabilities,
    const QStringList &availableLetters, QWidget *parent)
    : QWizard(parent), m_regionSize(region.size)
{
    setWindowTitle("New Simple Volume Wizard");
    configureWizard(this);
    addPage(simplePage("Welcome to the New Simple Volume Wizard",
        {"This wizard helps you create a simple volume on a disk.", "To continue, click Next."}));

    auto *sizePage = new QWizardPage;
    auto *sizeLayout = new QVBoxLayout(sizePage);
    sizeLayout->addWidget(wizardHeading("Specify Volume Size"));
    sizeLayout->addWidget(new QLabel("Choose a volume size that is between the maximum and minimum sizes."));
    auto *sizeForm = new QFormLayout;
    const quint64 maximumMiB = region.size / MiB;
    const int maximum = int(qMin<quint64>(maximumMiB, std::numeric_limits<int>::max()));
    auto *maxValue = new QLabel(QString::number(maximum));
    auto *minValue = new QLabel("8");
    auto *size = new QSpinBox;
    size->setObjectName("simpleVolumeSizeMiB");
    size->setRange(8, qMax(8, maximum));
    size->setValue(qMax(8, maximum));
    size->setSuffix(" MB");
    sizeForm->addRow("Maximum disk space in MB:", maxValue);
    sizeForm->addRow("Minimum disk space in MB:", minValue);
    sizeForm->addRow("Simple volume size in MB:", size);
    sizeLayout->addLayout(sizeForm);
    sizeLayout->addStretch();
    addPage(sizePage);

    auto *assignPage = new QWizardPage;
    auto *assignLayout = new QVBoxLayout(assignPage);
    assignLayout->addWidget(wizardHeading("Assign Drive Letter or Path"));
    auto *assignText = new QLabel("For easier access, you can assign a drive letter or drive path to your partition.");
    assignText->setWordWrap(true);
    assignLayout->addWidget(assignText);
    auto *letterRow = new QHBoxLayout;
    auto *letterRadio = new QRadioButton("Assign the following drive letter:");
    letterRadio->setObjectName("assignDriveLetter");
    letterRadio->setChecked(true);
    auto *letters = new QComboBox;
    letters->setObjectName("driveLetter");
    for (const QString &letter : availableLetters) letters->addItem(letter);
    letterRow->addWidget(letterRadio);
    letterRow->addWidget(letters);
    letterRow->addStretch();
    assignLayout->addLayout(letterRow);
    auto *folderRow = new QHBoxLayout;
    auto *folderRadio = new QRadioButton("Mount in the following empty folder:");
    folderRadio->setObjectName("assignFolder");
    auto *folder = new QLineEdit;
    folder->setObjectName("mountFolder");
    auto *browse = new QPushButton("Browse...");
    folderRow->addWidget(folderRadio);
    folderRow->addWidget(folder);
    folderRow->addWidget(browse);
    assignLayout->addLayout(folderRow);
    auto *noneRadio = new QRadioButton("Do not assign a drive letter or drive path");
    noneRadio->setObjectName("assignNone");
    assignLayout->addWidget(noneRadio);
    auto *assignmentGroup = new QButtonGroup(assignPage);
    assignmentGroup->addButton(letterRadio);
    assignmentGroup->addButton(folderRadio);
    assignmentGroup->addButton(noneRadio);
    connect(browse, &QPushButton::clicked, assignPage, [folder, assignPage] {
        const QString path = QFileDialog::getExistingDirectory(assignPage, "Select Empty Folder");
        if (!path.isEmpty()) folder->setText(path);
    });
    connect(folderRadio, &QRadioButton::toggled, folder, &QWidget::setEnabled);
    connect(letterRadio, &QRadioButton::toggled, letters, &QWidget::setEnabled);
    folder->setEnabled(false);
    assignLayout->addStretch();
    addPage(assignPage);

    auto *formatPage = new QWizardPage;
    auto *formatLayout = new QVBoxLayout(formatPage);
    formatLayout->addWidget(wizardHeading("Format Partition"));
    formatLayout->addWidget(new QLabel("To store data on this volume, you must format it first."));
    auto *noFormat = new QRadioButton("Do not format this volume");
    noFormat->setObjectName("doNotFormat");
    auto *doFormat = new QRadioButton("Format this volume with the following settings:");
    doFormat->setObjectName("doFormat");
    doFormat->setChecked(true);
    auto *formatGroup = new QButtonGroup(formatPage);
    formatGroup->addButton(noFormat);
    formatGroup->addButton(doFormat);
    formatLayout->addWidget(noFormat);
    formatLayout->addWidget(doFormat);
    auto *settings = new QWidget;
    auto *formatForm = new QFormLayout(settings);
    auto *fileSystem = new QComboBox;
    fileSystem->setObjectName("fileSystem");
    populateFileSystems(fileSystem, capabilities);
    auto *allocation = new QComboBox;
    allocation->addItem("Default");
    auto *label = new QLineEdit("New Volume");
    label->setObjectName("volumeLabel");
    auto *quick = new QCheckBox("Perform a quick format");
    quick->setObjectName("quickFormat");
    quick->setChecked(true);
    auto *compression = new QCheckBox("Enable file and folder compression");
    compression->setEnabled(false);
    compression->setToolTip("Filesystem compression is not exposed by the safe UDisks2 formatting API.");
    formatForm->addRow("File system:", fileSystem);
    formatForm->addRow("Allocation unit size:", allocation);
    formatForm->addRow("Volume label:", label);
    formatForm->addRow({}, quick);
    formatForm->addRow({}, compression);
    formatLayout->addWidget(settings);
    formatLayout->addStretch();
    connect(doFormat, &QRadioButton::toggled, settings, &QWidget::setEnabled);
    addPage(formatPage);

    auto *summaryPage = new QWizardPage;
    auto *summaryLayout = new QVBoxLayout(summaryPage);
    summaryLayout->addWidget(wizardHeading("Completing the New Simple Volume Wizard"));
    summaryLayout->addWidget(new QLabel("You selected the following settings:"));
    auto *summary = new QTextEdit;
    summary->setReadOnly(true);
    summary->setObjectName("newVolumeSummary");
    summaryLayout->addWidget(summary);
    connect(this, &QWizard::currentIdChanged, this, [this, summary, capabilities](int id) {
        if (id != pageIds().last()) return;
        const NewVolumeOptions value = options();
        summary->setPlainText(QString("Volume type: Simple Volume\n"
                                      "Volume size: %1 MB\n"
                                      "Drive letter or path: %2\n"
                                      "File system: %3\n"
                                      "Allocation unit size: Default\n"
                                      "Volume label: %4")
            .arg(value.sizeBytes / MiB)
            .arg(value.assignment == NewVolumeOptions::None ? "None"
                 : value.assignment == NewVolumeOptions::Folder ? value.mountFolder
                 : value.driveLetter)
            .arg(value.format ? formatName(value.fileSystem, capabilities) : "Do not format")
            .arg(value.format ? value.label : "—"));
    });
    addPage(summaryPage);
}

NewVolumeOptions NewSimpleVolumeWizard::options() const
{
    NewVolumeOptions result;
    const auto *size = findChild<QSpinBox *>("simpleVolumeSizeMiB");
    const auto *letters = findChild<QComboBox *>("driveLetter");
    const auto *folder = findChild<QLineEdit *>("mountFolder");
    const auto *assignFolder = findChild<QRadioButton *>("assignFolder");
    const auto *assignNone = findChild<QRadioButton *>("assignNone");
    const auto *doFormat = findChild<QRadioButton *>("doFormat");
    const auto *fileSystem = findChild<QComboBox *>("fileSystem");
    const auto *label = findChild<QLineEdit *>("volumeLabel");
    const auto *quick = findChild<QCheckBox *>("quickFormat");
    result.sizeBytes = quint64(size ? size->value() : 0) * MiB;
    result.driveLetter = letters ? letters->currentText() : QString{};
    result.mountFolder = folder ? folder->text().trimmed() : QString{};
    result.assignment = assignNone && assignNone->isChecked() ? NewVolumeOptions::None
        : assignFolder && assignFolder->isChecked() ? NewVolumeOptions::Folder
                                                    : NewVolumeOptions::DriveLetter;
    result.format = doFormat && doFormat->isChecked();
    result.fileSystem = fileSystem ? fileSystem->currentData().toString() : QString{};
    result.label = label ? label->text().trimmed() : QString{};
    result.quickFormat = quick && quick->isChecked();
    return result;
}

FormatVolumeDialog::FormatVolumeDialog(const QString &volumeName, const QString &currentLabel,
                                       const QString &currentFileSystem,
                                       const QList<FileSystemCapability> &capabilities,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Format " + volumeName);
    setModal(true);
    resize(430, 300);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_label = new QLineEdit(currentLabel);
    m_fileSystem = new QComboBox;
    populateFileSystems(m_fileSystem, capabilities, currentFileSystem);
    auto *allocation = new QComboBox;
    allocation->addItem("Default");
    m_quick = new QCheckBox("Perform a quick format");
    m_quick->setChecked(true);
    auto *compression = new QCheckBox("Enable file and folder compression");
    compression->setEnabled(false);
    form->addRow("Volume label:", m_label);
    form->addRow("File system:", m_fileSystem);
    form->addRow("Allocation unit size:", allocation);
    form->addRow({}, m_quick);
    form->addRow({}, compression);
    layout->addLayout(form);
    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

FormatVolumeOptions FormatVolumeDialog::options() const
{
    return {m_fileSystem->currentData().toString(), m_label->text().trimmed(), m_quick->isChecked()};
}

ExtendVolumeWizard::ExtendVolumeWizard(int diskNumber, quint64 currentSize,
                                       quint64 availableSize, QWidget *parent)
    : QWizard(parent), m_currentSize(currentSize), m_availableSize(availableSize)
{
    setWindowTitle("Extend Volume Wizard");
    configureWizard(this);
    addPage(simplePage("Welcome to the Extend Volume Wizard",
        {"This wizard helps you increase the size of simple and spanned volumes.",
         "You can add space from one or more disks.", "To continue, click Next."}));
    auto *selectPage = new QWizardPage;
    auto *layout = new QVBoxLayout(selectPage);
    layout->addWidget(wizardHeading("Select Disks"));
    layout->addWidget(new QLabel("You can use space on one or more disks to extend the volume."));
    auto *lists = new QHBoxLayout;
    auto *availableBox = new QGroupBox("Available:");
    auto *availableLayout = new QVBoxLayout(availableBox);
    availableLayout->addWidget(new QLabel("No additional disks available"));
    auto *selectedBox = new QGroupBox("Selected:");
    auto *selectedLayout = new QVBoxLayout(selectedBox);
    selectedLayout->addWidget(new QLabel(QString("Disk %1    %2 MB").arg(diskNumber)
        .arg(availableSize / MiB)));
    lists->addWidget(availableBox);
    auto *middle = new QVBoxLayout;
    auto *add = new QPushButton("Add >"); add->setEnabled(false);
    auto *remove = new QPushButton("< Remove"); remove->setEnabled(false);
    middle->addStretch(); middle->addWidget(add); middle->addWidget(remove); middle->addStretch();
    lists->addLayout(middle);
    lists->addWidget(selectedBox);
    layout->addLayout(lists);
    auto *form = new QFormLayout;
    form->addRow("Total volume size in megabytes (MB):",
                 new QLabel(QString::number((currentSize + availableSize) / MiB)));
    form->addRow("Maximum available space in MB:", new QLabel(QString::number(availableSize / MiB)));
    auto *amount = new QSpinBox;
    amount->setObjectName("extendAmountMiB");
    const int maximum = int(qMin<quint64>(availableSize / MiB, std::numeric_limits<int>::max()));
    amount->setRange(8, qMax(8, maximum));
    amount->setValue(qMax(8, maximum));
    amount->setSuffix(" MB");
    form->addRow("Select the amount of space in MB:", amount);
    layout->addLayout(form);
    addPage(selectPage);
    auto *summary = new QWizardPage;
    auto *summaryLayout = new QVBoxLayout(summary);
    summaryLayout->addWidget(wizardHeading("Completing the Extend Volume Wizard"));
    summaryLayout->addWidget(new QLabel("You selected the following settings:"));
    auto *details = new QTextEdit;
    details->setReadOnly(true);
    summaryLayout->addWidget(details);
    connect(this, &QWizard::currentIdChanged, this, [this, details, diskNumber](int id) {
        if (id == pageIds().last())
            details->setPlainText(QString("Disk selected: Disk %1\nSpace to add: %2 MB\n"
                                          "Final volume size: %3 MB")
                .arg(diskNumber).arg(additionalBytes() / MiB)
                .arg((m_currentSize + additionalBytes()) / MiB));
    });
    addPage(summary);
}

quint64 ExtendVolumeWizard::additionalBytes() const
{
    const auto *amount = findChild<QSpinBox *>("extendAmountMiB");
    return quint64(amount ? amount->value() : 0) * MiB;
}
