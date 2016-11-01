/*
 *  Copyright (c) 2009 Boudewijn Rempt <boud@valdyas.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include "psd_export.h"

#include <QCheckBox>
#include <QSlider>
#include <QFileInfo>
#include <QApplication>

#include <kpluginfactory.h>

#include <KisExportCheckRegistry.h>
#include <KisImportExportManager.h>
#include <ImageSizeCheck.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorSpaceConstants.h>

#include <KisDocument.h>
#include <kis_image.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_paint_device.h>

#include "psd_saver.h"

class KisExternalLayer;

K_PLUGIN_FACTORY_WITH_JSON(ExportFactory, "krita_psd_export.json", registerPlugin<psdExport>();)

bool checkHomogenity(KisNodeSP root, const KoColorSpace* cs)
{
    bool res = true;
    KisNodeSP child = root->firstChild();
    while (child) {
        if (child->childCount() > 0) {
            res = checkHomogenity(child, cs);
            if (res == false) {
                break;
            }
        }
        KisLayer *layer = dynamic_cast<KisLayer*>(child.data());
        if (layer) {
            if (layer->colorSpace() != cs) {
                res = false;
                break;
            }
        }
        child = child->nextSibling();
    }
    return res;
}

psdExport::psdExport(QObject *parent, const QVariantList &) : KisImportExportFilter(parent)
{
}

psdExport::~psdExport()
{
}

KisImportExportFilter::ConversionStatus psdExport::convert(KisDocument *document, QIODevice *io,  KisPropertiesConfigurationSP /*configuration*/)
{
    PSDSaver psdSaver(document);
    KisImageBuilder_Result res;

    if ((res = psdSaver.buildFile(io)) == KisImageBuilder_RESULT_OK) {
        dbgFile <<"success !";
        return KisImportExportFilter::OK;
    }
    dbgFile <<" Result =" << res;
    return KisImportExportFilter::InternalError;
}

void psdExport::initializeCapabilities()
{
    addCapability(KisExportCheckRegistry::instance()->get("PSDLayerStyleCheck")->create(KisExportCheckBase::SUPPORTED));
    addCapability(KisExportCheckRegistry::instance()->get("NodeTypeCheck/KisGroupLayer")->create(KisExportCheckBase::SUPPORTED));
    addCapability(KisExportCheckRegistry::instance()->get("MultiLayerCheck")->create(KisExportCheckBase::SUPPORTED));
    addCapability(KisExportCheckRegistry::instance()->get("sRGBProfileCheck")->create(KisExportCheckBase::SUPPORTED));
    addCapability(KisExportCheckRegistry::instance()->get("MultiLayerCheck")->create(KisExportCheckBase::SUPPORTED));
    addCapability(KisExportCheckRegistry::instance()->get("ColorModelHomogenousCheck")->create(KisExportCheckBase::UNSUPPORTED, i18nc("image conversion warning", "Your image contains one or more layers with a color model that is different from the image.")));

    ImageSizeCheckFactory *factory = dynamic_cast<ImageSizeCheckFactory*>(KisExportCheckRegistry::instance()->get("ImageSizeCheck"));
    if (factory) {
        addCapability(factory->create(30000, 30000, KisExportCheckBase::SUPPORTED));
    }

    QList<QPair<KoID, KoID> > supportedColorModels;
    supportedColorModels << QPair<KoID, KoID>()
            << QPair<KoID, KoID>(RGBAColorModelID, Integer8BitsColorDepthID)
            << QPair<KoID, KoID>(RGBAColorModelID, Integer16BitsColorDepthID)
            << QPair<KoID, KoID>(RGBAColorModelID, Float16BitsColorDepthID)
            << QPair<KoID, KoID>(RGBAColorModelID, Float32BitsColorDepthID)
            << QPair<KoID, KoID>(GrayAColorModelID, Integer8BitsColorDepthID)
            << QPair<KoID, KoID>(GrayAColorModelID, Integer16BitsColorDepthID)
            << QPair<KoID, KoID>(CMYKAColorModelID, Integer8BitsColorDepthID)
            << QPair<KoID, KoID>(CMYKAColorModelID, Integer16BitsColorDepthID)
            << QPair<KoID, KoID>(LABAColorModelID, Integer8BitsColorDepthID)
            << QPair<KoID, KoID>(LABAColorModelID, Integer16BitsColorDepthID);
    addSupportedColorModels(supportedColorModels, "PSD");

}

#include <psd_export.moc>

