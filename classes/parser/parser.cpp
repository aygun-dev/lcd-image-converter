/*
 * LCD Image Converter. Converts images and fonts for embedded applications.
 * Copyright (C) 2010 riuson
 * mailto: riuson@gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */

#include "parser.h"
//-----------------------------------------------------------------------------
#include <QFile>
#include <QTextStream>
#include <QTextCodec>

#include "idocument.h"
#include "datacontainer.h"
#include "bitmaphelper.h"
#include "converterhelper.h"
#include "preset.h"
#include "prepareoptions.h"
#include "matrixoptions.h"
#include "imageoptions.h"
#include "fontoptions.h"
#include "templateoptions.h"
//-----------------------------------------------------------------------------
Parser::Parser(QObject *parent, TemplateType templateType) :
        QObject(parent)
{
    this->mPreset = new Preset(this);

    this->mSelectedPresetName = Preset::currentName();

    this->mPreset->load(this->mSelectedPresetName);

    if (templateType == TypeImage)
        this->mTemplateFileName = this->mPreset->templates()->image();
    else
        this->mTemplateFileName = this->mPreset->templates()->font();
}
//-----------------------------------------------------------------------------
Parser::~Parser()
{
    delete this->mPreset;
}
//-----------------------------------------------------------------------------
QString Parser::name()
{
    //IConverter *options = dynamic_cast<IConverter *>(this->mConverters.value(this->mSelectedConverterName));
    //return options->name();
    return this->mSelectedPresetName;
}
//-----------------------------------------------------------------------------
QString Parser::convert(IDocument *document, QMap<QString, QString> &tags) const
{
    QString result;

    QString templateString;

    QFile file(this->mTemplateFileName);

    if (!file.exists())
        file.setFileName(":/templates/image_convert");

    if (file.open(QIODevice::ReadOnly))
    {
        QTextStream stream(&file);
        templateString = stream.readAll();
        file.close();
    }
    tags.insert("templateFile", file.fileName());

    QRegExp regImageData = this->expression(Parser::ImageData);
    regImageData.setMinimal(true);
    if (regImageData.indexIn(templateString) >= 0)
    {
        QString strImageDataIndent = regImageData.cap(1);
        if (strImageDataIndent.isEmpty())
            strImageDataIndent = "    ";
        tags["imageDataIndent"] = strImageDataIndent;
    }
    else
    {
        tags["imageDataIndent"] = "    ";
    }

    this->addMatrixInfo(tags);

    this->parse(templateString, result, tags, document);

    return result;
}
//-----------------------------------------------------------------------------
void Parser::parse(const QString &templateString,
                      QString &resultString,
                      QMap<QString, QString> &tags,
                      IDocument *doc) const
{
    int index = 0;
    int prevIndex = 0;
    QRegExp regTag = this->expression(Parser::TagName);
    regTag.setMinimal(true);
    int capturedLength = 0;
    while ((index = regTag.indexIn(templateString, index + capturedLength)) >= 0)
    {
        capturedLength = regTag.cap(0).length();
        if (index > prevIndex)
        {
            resultString.append(templateString.mid(prevIndex, index - prevIndex));
        }
        QString tagName = regTag.cap(3);
        // if block starts
        if (regTag.cap(2) == "start_block_")
        {
            QRegExp contentReg = this->expression(Parser::Content, tagName);
            contentReg.setMinimal(true);
            if (contentReg.indexIn(templateString, index) >= 0)
            {
                QString content = contentReg.cap(3);
                content = content.trimmed();
                QString temp;

                if (tagName == "images_table")
                {
                    this->parseImagesTable(content, temp, tags, doc);
                }
                else
                {
                    this->parse(content, temp, tags, doc);
                }
                resultString.append(temp);

                capturedLength = contentReg.cap(0).length();
                prevIndex = index + capturedLength;
            }
        }
        else
        {
            if (tags.contains(tagName))
                resultString.append(tags.value(tagName));
            else
                resultString.append("<value not defined>");
            prevIndex = index + regTag.cap(0).length();
        }
    }
    int last = prevIndex;
    if (last < templateString.length() - 1)
    {
        resultString.append(templateString.mid(last, templateString.length() - last));
    }
}
//-----------------------------------------------------------------------------
void Parser::parseBlocks(const QString &templateString,
                            QString &resultString,
                            QMap<QString, QString> &tags,
                            IDocument *doc) const
{
    // capture block
    QRegExp startReg = this->expression(Parser::BlockStart);
    startReg.setMinimal(true);
    int index = -1;
    while ((index = startReg.indexIn(templateString, index + 1)) >= 0)
    {
        QString blockName = startReg.cap(2);
        QRegExp endReg = this->expression(Parser::BlockEnd, blockName);
        endReg.setMinimal(true);
        // capture block's content
        QRegExp contentReg = this->expression(Parser::Content, blockName);
        contentReg.setMinimal(true);
        int index2 = index - 1;
        while ((index2 = contentReg.indexIn(templateString, index2 + 1)) >= 0)
        {
            QString content = contentReg.cap(3);
            //index2 += content.length();
            content = content.trimmed();

            QString contentParsed;
            if (blockName == "images_table")
            {
                this->parseImagesTable(content, contentParsed, tags, doc);
            }
            else
            {
                this->parseSimple(content, contentParsed, tags, doc);
            }
            resultString.append(contentParsed);
        }
    }
}
//-----------------------------------------------------------------------------
void Parser::parseImagesTable(const QString &templateString,
                                 QString &resultString,
                                 QMap<QString, QString> &tags,
                                 IDocument *doc) const
{
    DataContainer *data = doc->dataContainer();
    QString imageString;
    QListIterator<QString> it(data->keys());
    it.toFront();
    tags["imagesCount"] = QString("%1").arg(data->count());
    while (it.hasNext())
    {
        QString key = it.next();
        QImage image = QImage(*data->image(key));

        // width and height must be written before image changes
        tags["width"] = QString("%1").arg(image.width());
        tags["height"] = QString("%1").arg(image.height());

        QImage imagePrepared;
        ConverterHelper::prepareImage(this->mPreset, &image, &imagePrepared);

        // conversion from image to strings
        QVector<quint32> sourceData;
        int sourceWidth, sourceHeight;
        ConverterHelper::pixelsData(this->mPreset, &imagePrepared, &sourceData, &sourceWidth, &sourceHeight);

        ConverterHelper::processPixels(this->mPreset, &sourceData);

        QVector<quint32> packedData;
        int packedWidth, packedHeight;
        ConverterHelper::packData(
                    this->mPreset,
                    &sourceData, sourceWidth, sourceHeight,
                    &packedData, &packedWidth, &packedHeight);

        QVector<quint32> reorderedData;
        int reorderedWidth, reorderedHeight;
        ConverterHelper::reorder(
                    this->mPreset,
                    &packedData, packedWidth, packedHeight,
                    &reorderedData, &reorderedWidth, &reorderedHeight);

        QVector<quint32> compressedData;
        int compressedWidth, compressedHeight;
        ConverterHelper::compressData(this->mPreset, &reorderedData, reorderedWidth, reorderedHeight, &compressedData, &compressedWidth, &compressedHeight);

        QString dataString = ConverterHelper::dataToString(this->mPreset, &compressedData, compressedWidth, compressedHeight, "0x");
        dataString.replace("\n", "\n" + tags["imageDataIndent"]);

        // end of conversion

        bool useBom = this->mPreset->font()->bom();
        QString encoding = this->mPreset->font()->encoding();

        QString charCode = this->hexCode(key.at(0), encoding, useBom);

        tags["blocksCount"] = QString("%1").arg(compressedData.size());
        tags["imageData"] = dataString;
        tags["charCode"] = charCode;
        if (it.hasNext())
            tags["comma"] = ",";
        else
            tags["comma"] = "";

        if (key.contains("@"))
        {
            key.replace("@", "(a)");
            tags["charText"] = key;
        }
        else
            tags["charText"] = key.left(1);

        this->parseSimple(templateString, imageString, tags, doc);
        resultString.append("\n");
        resultString.append(imageString);
    }
}
//-----------------------------------------------------------------------------
void Parser::parseSimple(const QString &templateString,
                            QString &resultString,
                            QMap<QString, QString> &tags,
                            IDocument *doc) const
{
    Q_UNUSED(doc);
    QRegExp regTag = this->expression(Parser::TagName);
    regTag.setMinimal(true);
    resultString = templateString;
    while (regTag.indexIn(resultString) >= 0)
    {
        QString tag = regTag.cap(0);
        QString tagName = regTag.cap(3);
        if (tags.contains(tagName))
            resultString.replace(tag, tags.value(tagName));
        else
            resultString.replace(tag, "<value not defined>");
    }
}
//-----------------------------------------------------------------------------
QString Parser::hexCode(const QChar &ch, const QString &encoding, bool bom) const
{
    QString result;
    QTextCodec *codec = QTextCodec::codecForName(encoding.toAscii());

    QByteArray codeArray = codec->fromUnicode(&ch, 1);

    quint64 code = 0;
    for (int i = 0; i < codeArray.count() && i < 8; i++)
    {
        code = code << 8;
        code |= (quint8)codeArray.at(i);
    }

    if (encoding.contains("UTF-16"))
    {
        // reorder bytes
        quint64 a =
                ((code & 0x000000000000ff00ULL) >> 8) |
                ((code & 0x00000000000000ffULL) << 8);
        code &= 0xffffffffffff0000ULL;
        code |= a;

        if (bom)
        {
            // 0xfeff00c1
            result = QString("%1").arg(code, 8, 16, QChar('0'));
        }
        else
        {
            // 0x00c1
            code &= 0x000000000000ffffULL;
            result = QString("%1").arg(code, 4, 16, QChar('0'));
        }
    }
    else if (encoding.contains("UTF-32"))
    {
        // reorder bytes
        quint64 a =
                ((code & 0x00000000ff000000ULL) >> 24) |
                ((code & 0x0000000000ff0000ULL) >> 8) |
                ((code & 0x000000000000ff00ULL) << 8) |
                ((code & 0x00000000000000ffULL) << 24);
        code &= 0xffffffff00000000ULL;
        code |= a;

        if (bom)
        {
            // 0x0000feff000000c1
            result = QString("%1").arg(code, 16, 16, QChar('0'));
        }
        else
        {
            // 0x000000c1
            code &= 0x00000000ffffffffULL;
            result = QString("%1").arg(code, 8, 16, QChar('0'));
        }
    }
    else
    {
        result = QString("%1").arg(code, codeArray.count() * 2, 16, QChar('0'));
    }


    return result;
}
//-----------------------------------------------------------------------------
void Parser::addMatrixInfo(QMap<QString, QString> &tags) const
{
    // byte order
    if (this->mPreset->image()->bytesOrder() == BytesOrderLittleEndian)
        tags.insert("bytesOrder", "little-endian");
    else
        tags.insert("bytesOrder", "big-endian");

    // data block size
    int dataBlockSize = (this->mPreset->image()->blockSize() + 1) * 8;
    tags.insert("dataBlockSize", QString("%1").arg(dataBlockSize));

    // scan main direction
    switch (this->mPreset->prepare()->scanMain())
    {
        case TopToBottom:
            tags.insert("scanMain", "top to bottom");
            break;
        case BottomToTop:
            tags.insert("scanMain", "bottom to top");
            break;
        case LeftToRight:
            tags.insert("scanMain", "left to right");
            break;
        case RightToLeft:
            tags.insert("scanMain", "right to left");
            break;
    }

    // scan sub direction
    if (this->mPreset->prepare()->scanSub())
        tags.insert("scanSub", "forward");
    else
        tags.insert("scanSub", "backward");

    // bands
    if (this->mPreset->prepare()->bandScanning())
        tags.insert("bands", "yes");
    else
        tags.insert("bands", "no");
    int bandWidth = this->mPreset->prepare()->bandWidth();
    tags.insert("bandWidth", QString("%1").arg(bandWidth));


    // inversion
    if (this->mPreset->prepare()->inverse())
        tags.insert("inverse", "yes");
    else
        tags.insert("inverse", "no");

    // bom
    if (this->mPreset->font()->bom())
        tags.insert("bom", "yes");
    else
        tags.insert("bom", "no");

    // encoding
    tags.insert("encoding", this->mPreset->font()->encoding());

    // split to rows
    if (this->mPreset->image()->splitToRows())
        tags.insert("splitToRows", "yes");
    else
        tags.insert("splitToRows", "no");

    // compression
    if (this->mPreset->image()->compressionRle())
        tags.insert("rle", "yes");
    else
        tags.insert("rle", "no");

    // preset name
    tags.insert("preset", this->mSelectedPresetName);

    // conversion type
    tags.insert("convType", this->mPreset->prepare()->convTypeName());

    // monochrome type
    if (this->mPreset->prepare()->convType() == ConversionTypeMonochrome)
    {
        tags.insert("monoType", this->mPreset->prepare()->monoTypeName());
        tags.insert("edge", QString("%1").arg(this->mPreset->prepare()->edge()));
    }
    else
    {
        tags.insert("monoType", "not used");
        tags.insert("edge", "not used");
    }

    // bits per pixel
    quint32 maskUsed = this->mPreset->matrix()->maskUsed();
    int bitsPerPixel = 0;
    while (maskUsed > 0)
    {
        if ((maskUsed & 0x00000001) != 0)
            bitsPerPixel++;
        maskUsed = maskUsed >> 1;
    }
    tags.insert("bpp", QString("%1").arg(bitsPerPixel));
}
//-----------------------------------------------------------------------------
QRegExp Parser::expression(ExpType type, const QString &name) const
{
    QString result;

    switch (type)
    {
    case BlockStart:
        // 2
        result = "(\\@|\\$\\()start_block_(.+)(?=\\s)";
        break;
    case BlockEnd:
        result = "(\\@|\\$\\()end_block_" + name;
        break;
    case ImageData:
        // 1
        result = "([\\t\\ ]+)(\\@|\\$\\()imageData(\\@|\\))";
        break;
    case Content:
        // 3
        result = "(\\@|\\$\\()start_block_" + name + "(\\@|\\))(.+)(\\@|\\$\\()end_block_" + name + "(\\@|\\))";
        break;
    case TagName:
    default:
        // 2, 3
        result = "(\\@|\\$\\()(start_block_)?(.+)(\\@|\\))";
        break;
    }

    return QRegExp(result);
}
//-----------------------------------------------------------------------------