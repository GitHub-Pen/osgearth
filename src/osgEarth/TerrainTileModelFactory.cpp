/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2014 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include <osgEarth/TerrainTileModelFactory>
#include <osgEarth/Registry>
#include <osgEarth/ImageUtils>
#include <osgEarth/ImageToHeightFieldConverter>
#include <osgEarth/PatchLayer>
#include <osgEarth/MapOptions>
#include <osgEarth/MapFrame>

#include <osg/Texture2D>

#define LC "[TerrainTileModelFactory] "

using namespace osgEarth;

//.........................................................................

TerrainTileModelFactory::TerrainTileModelFactory(const TerrainOptions& options) :
_options         ( options ),
_heightFieldCache( true, 128 )
{
    _heightFieldCacheEnabled = (::getenv("OSGEARTH_MEMORY_PROFILE") == 0L);

    // Create an empty texture that we can use as a placeholder
    _emptyTexture = new osg::Texture2D(ImageUtils::createEmptyImage());
}

TerrainTileModel*
TerrainTileModelFactory::createTileModel(const MapFrame&                  frame,
                                         const TileKey&                   key,
                                         const CreateTileModelFilter&     filter,
                                         const TerrainEngineRequirements* requirements,
                                         ProgressCallback*                progress)
{
    // Make a new model:
    osg::ref_ptr<TerrainTileModel> model = new TerrainTileModel(
        key,
        frame.getRevision() );

    // assemble all the components:
    //addImageLayers(model.get(), frame, requirements, key, filter, progress);
    addColorLayers(model.get(), frame, requirements, key, filter, progress);

    addPatchLayers(model.get(), frame, key, filter, progress);

    if ( requirements == 0L || requirements->elevationTexturesRequired() )
    {
        unsigned border = requirements->elevationBorderRequired() ? 1u : 0u;

        addElevation( model.get(), frame, key, filter, border, progress );
    }

#if 0
    if ( requirements == 0L || requirements->normalTexturesRequired() )
    {
        addNormalMap( model.get(), frame, key, progress );
    }
#endif

    // done.
    return model.release();
}

void
TerrainTileModelFactory::addColorLayers(TerrainTileModel* model,
                                        const MapFrame&   frame,
                                        const TerrainEngineRequirements* reqs,
                                        const TileKey&    key,
                                        const CreateTileModelFilter& filter,
                                        ProgressCallback* progress)
{
    OE_START_TIMER(fetch_image_layers);

    int order = 0;

    for (LayerVector::const_iterator i = frame.layers().begin();
        i != frame.layers().end();
        ++i)
    {
        Layer* layer = i->get();

        if (layer->getRenderType() != layer->RENDERTYPE_TILE)
            continue;

        if (!layer->getEnabled())
            continue;

        if (!filter.accept(layer))
            continue;

        ImageLayer* imageLayer = dynamic_cast<ImageLayer*>(layer);
        if (imageLayer)
        {
            osg::Texture* tex = 0L;
            osg::Matrixf textureMatrix;

            if (imageLayer->isKeyInLegalRange(key) && imageLayer->mayHaveDataInExtent(key.getExtent()))
            {
                if (imageLayer->createTextureSupported())
                {
                    tex = imageLayer->createTexture( key, progress, textureMatrix );
                }

                else
                {
                    GeoImage geoImage = imageLayer->createImage( key, progress );
           
                    if ( geoImage.valid() )
                    {
                        if ( imageLayer->isCoverage() )
                            tex = createCoverageTexture(geoImage.getImage(), imageLayer);
                        else
                            tex = createImageTexture(geoImage.getImage(), imageLayer);
                    }
                }
            }
        
            // if this is the first LOD, and the engine requires that the first LOD
            // be populated, make an empty texture if we didn't get one.
            if (tex == 0L &&
                _options.firstLOD() == key.getLOD() &&
                reqs && reqs->fullDataAtFirstLodRequired())
            {
                tex = _emptyTexture.get();
            }
         
            if (tex)
            {
                TerrainTileImageLayerModel* layerModel = new TerrainTileImageLayerModel();

                layerModel->setImageLayer(imageLayer);

                layerModel->setTexture(tex);
                layerModel->setMatrix(new osg::RefMatrixf(textureMatrix));

                model->colorLayers().push_back(layerModel);

                if (imageLayer->isShared())
                    model->sharedLayers().push_back(layerModel);

                if (imageLayer->isDynamic())
                    model->setRequiresUpdateTraverse(true);
            }
        }

        else // non-image kind of TILE layer:
        {
            TerrainTileColorLayerModel* colorModel = new TerrainTileColorLayerModel();
            colorModel->setLayer(layer);
            model->colorLayers().push_back(colorModel);
        }
    }

    if (progress)
        progress->stats()["fetch_imagery_time"] += OE_STOP_TIMER(fetch_image_layers);
}

void
TerrainTileModelFactory::addImageLayers(TerrainTileModel* model,
                                        const MapFrame&   frame,
                                        const TerrainEngineRequirements* reqs,
                                        const TileKey&    key,
                                        const CreateTileModelFilter& filter,
                                        ProgressCallback* progress)
{
    OE_START_TIMER(fetch_image_layers);

    int order = 0;

    ImageLayerVector imageLayers;
    frame.getLayers(imageLayers);

    for(ImageLayerVector::const_iterator i = imageLayers.begin();
        i != imageLayers.end();
        ++i, ++order )
    {
        ImageLayer* layer = i->get();

        if (!filter.accept(layer))
            continue;

        if (!layer->getEnabled())
            continue;

        osg::Texture* tex = 0L;
        osg::Matrixf textureMatrix;

        if (layer->isKeyInLegalRange(key) && layer->mayHaveDataInExtent(key.getExtent()))
        {
            if (layer->createTextureSupported())
            {
                tex = layer->createTexture( key, progress, textureMatrix );
            }

            else
            {
                GeoImage geoImage = layer->createImage( key, progress );
           
                if ( geoImage.valid() )
                {
                    if ( layer->isCoverage() )
                        tex = createCoverageTexture(geoImage.getImage(), layer);
                    else
                        tex = createImageTexture(geoImage.getImage(), layer);
                }
            }
        }
        
        // if this is the first LOD, and the engine requires that the first LOD
        // be populated, make an empty texture if we didn't get one.
        if (tex == 0L &&
            _options.firstLOD() == key.getLOD() &&
            reqs && reqs->fullDataAtFirstLodRequired())
        {
            tex = _emptyTexture.get();
        }
         
        if (tex)
        {
            TerrainTileImageLayerModel* layerModel = new TerrainTileImageLayerModel();

            layerModel->setImageLayer(layer);

            layerModel->setTexture(tex);
            layerModel->setMatrix(new osg::RefMatrixf(textureMatrix));

            model->colorLayers().push_back(layerModel);

            if (layer->isShared())
                model->sharedLayers().push_back(layerModel);

            if (layer->isDynamic())
                model->setRequiresUpdateTraverse(true);
        }
    }

    if (progress)
        progress->stats()["fetch_imagery_time"] += OE_STOP_TIMER(fetch_image_layers);
}


void
TerrainTileModelFactory::addPatchLayers(TerrainTileModel* model,
                                        const MapFrame&   frame,
                                        const TileKey&    key,
                                        const CreateTileModelFilter& filter,
                                        ProgressCallback* progress)
{
    OE_START_TIMER(fetch_patch_layers);

    PatchLayerVector patchLayers;
    frame.getLayers(patchLayers);

    for(PatchLayerVector::const_iterator i = patchLayers.begin();
        i != patchLayers.end();
        ++i )
    {
        PatchLayer* layer = i->get();

        if (!filter.accept(layer))
            continue;

        if (!layer->getEnabled())
            continue;

        if (layer->getAcceptCallback() == 0L || layer->getAcceptCallback()->acceptKey(key))
        {
            PatchLayer::TileData* tileData = layer->createTileData(key);
            if (tileData)
            {
                TerrainTilePatchLayerModel* patchModel = new TerrainTilePatchLayerModel();
                patchModel->setPatchLayer(layer);
                patchModel->setTileData(tileData);

                model->patchLayers().push_back(patchModel);
            }
        }
    }

    if (progress)
        progress->stats()["fetch_patches_time"] += OE_STOP_TIMER(fetch_patch_layers);
}


void
TerrainTileModelFactory::addElevation(TerrainTileModel*            model,
                                      const MapFrame&              frame,
                                      const TileKey&               key,
                                      const CreateTileModelFilter& filter,
                                      unsigned                     border,
                                      ProgressCallback*            progress)
{    
    // make an elevation layer.
    OE_START_TIMER(fetch_elevation);

    if (!filter.empty() && !filter.elevation().isSetTo(true))
        return;

    const MapInfo& mapInfo = frame.getMapInfo();

    const osgEarth::ElevationInterpolation& interp =
        frame.getMapOptions().elevationInterpolation().get();

    // Request a heightfield from the map.
    osg::ref_ptr<osg::HeightField> mainHF;
    osg::ref_ptr<NormalMap> normalMap;

    bool hfOK = getOrCreateHeightField(frame, key, SAMPLE_FIRST_VALID, interp, border, mainHF, normalMap, progress) && mainHF.valid();

    if (hfOK == false && key.getLOD() == _options.firstLOD().get())
    {
        OE_DEBUG << LC << "No HF at key " << key.str() << ", making placeholder" << std::endl;
        mainHF = new osg::HeightField();
        mainHF->allocate(1, 1);
        mainHF->setHeight(0, 0, 0.0f);
        hfOK = true;
    }

    if (hfOK && mainHF.valid())
    {
        osg::ref_ptr<TerrainTileElevationModel> layerModel = new TerrainTileElevationModel();
        layerModel->setHeightField( mainHF.get() );

        // pre-calculate the min/max heights:
        for( unsigned col = 0; col < mainHF->getNumColumns(); ++col )
        {
            for( unsigned row = 0; row < mainHF->getNumRows(); ++row )
            {
                float h = mainHF->getHeight(col, row);
                if ( h > layerModel->getMaxHeight() )
                    layerModel->setMaxHeight( h );
                if ( h < layerModel->getMinHeight() )
                    layerModel->setMinHeight( h );
            }
        }        

        // needed for normal map generation
        model->heightFields().setNeighbor(0, 0, mainHF.get());

        // convert the heightfield to a 1-channel 32-bit fp image:
        ImageToHeightFieldConverter conv;
        osg::Image* hfImage = conv.convertToR32F(mainHF.get());

        if ( hfImage )
        {
            // Made an image, so store this as a texture with no matrix.
            osg::Texture* texture = createElevationTexture( hfImage );
            layerModel->setTexture( texture );
            model->elevationModel() = layerModel.get();
        }

        if (normalMap.valid())
        {
            TerrainTileImageLayerModel* layerModel = new TerrainTileImageLayerModel();
            layerModel->setName( "oe_normal_map" );

            // Made an image, so store this as a texture with no matrix.
            osg::Texture* texture = createNormalTexture(normalMap.get());
            layerModel->setTexture( texture );
            model->normalModel() = layerModel;
        }
    }

    if (progress)
        progress->stats()["fetch_elevation_time"] += OE_STOP_TIMER(fetch_elevation);
}

void
TerrainTileModelFactory::addNormalMap(TerrainTileModel* model,
                                      const MapFrame&   frame,
                                      const TileKey&    key,
                                      ProgressCallback* progress)
{
    OE_START_TIMER(fetch_normalmap);

    if (model->elevationModel().valid())
    {
        const osgEarth::ElevationInterpolation& interp =
            frame.getMapOptions().elevationInterpolation().get();

        // Can only generate the normal map if the center heightfield was built:
        osg::ref_ptr<osg::Image> image = HeightFieldUtils::convertToNormalMap(
            model->heightFields(),
            key.getProfile()->getSRS() );

        if (image.valid())
        {
            TerrainTileImageLayerModel* layerModel = new TerrainTileImageLayerModel();
            layerModel->setName( "oe_normal_map" );

            // Made an image, so store this as a texture with no matrix.
            osg::Texture* texture = createNormalTexture( image );
            layerModel->setTexture( texture );
            model->normalModel() = layerModel;
        }
    }

    if (progress)
        progress->stats()["fetch_normalmap_time"] += OE_STOP_TIMER(fetch_normalmap);
}

bool
TerrainTileModelFactory::getOrCreateHeightField(const MapFrame&                 frame,
                                                const TileKey&                  key,
                                                ElevationSamplePolicy           samplePolicy,
                                                ElevationInterpolation          interpolation,
                                                unsigned                        border,
                                                osg::ref_ptr<osg::HeightField>& out_hf,
                                                osg::ref_ptr<NormalMap>&        out_normalMap,
                                                ProgressCallback*               progress)
{
    // check the quick cache.
    HFCacheKey cachekey;
    cachekey._key          = key;
    cachekey._revision     = frame.getRevision();
    cachekey._samplePolicy = samplePolicy;

    if (progress)
        progress->stats()["hfcache_try_count"] += 1;

    bool hit = false;
    HFCache::Record rec;
    if ( _heightFieldCacheEnabled && _heightFieldCache.get(cachekey, rec) )
    {
        out_hf = rec.value()._hf.get();
        out_normalMap = rec.value()._normalMap.get();

        if (progress)
        {
            progress->stats()["hfcache_hit_count"] += 1;
            progress->stats()["hfcache_hit_rate"] = progress->stats()["hfcache_hit_count"]/progress->stats()["hfcache_try_count"];
        }

        return true;
    }

    if ( !out_hf.valid() )
    {
        out_hf = HeightFieldUtils::createReferenceHeightField(
            key.getExtent(),
            257, 257,           // base tile size for elevation data
            border,             // 1 sample border around the data makes it 259x259
            true);              // initialize to HAE (0.0) heights
    }

    if (!out_normalMap.valid())
    {
        //OE_INFO << "TODO: check terrain reqs\n";
        out_normalMap = new NormalMap(257, 257); // ImageUtils::createEmptyImage(257, 257);        
    }

    bool populated = frame.populateHeightFieldAndNormalMap(
        out_hf,
        out_normalMap,
        key,
        true, // convertToHAE
        progress );

#ifdef TREAT_ALL_ZEROS_AS_MISSING_TILE
    // check for a real tile with all zeros and treat it the same as non-existant data.
    if ( populated )
    {
        bool isEmpty = true;
        for(osg::FloatArray::const_iterator f = out_hf->getFloatArray()->begin(); f != out_hf->getFloatArray()->end(); ++f)
        {
            if ( (*f) != 0.0f )
            {
                isEmpty = false;
                break;
            }
        }
        if ( isEmpty )
        {
            populated = false;
        }
    }
#endif

    if ( populated )
    {
        // Treat Plate Carre specially by scaling the height values. (There is no need
        // to do this with an empty heightfield)
        const MapInfo& mapInfo = frame.getMapInfo();
        if ( mapInfo.isPlateCarre() )
        {
            HeightFieldUtils::scaleHeightFieldToDegrees( out_hf.get() );
        }

        // cache it.
        if (_heightFieldCacheEnabled )
        {
            HFCacheValue newValue;
            newValue._hf = out_hf.get();
            newValue._normalMap = out_normalMap.get();

            _heightFieldCache.insert( cachekey, newValue );
        }
    }

    return populated;
}

osg::Texture*
TerrainTileModelFactory::createImageTexture(osg::Image*       image,
                                            const ImageLayer* layer) const
{
    osg::Texture2D* tex = new osg::Texture2D( image );

    tex->setWrap( osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE );
    tex->setWrap( osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE );
    tex->setResizeNonPowerOfTwoHint(false);

    osg::Texture::FilterMode magFilter = 
        layer ? layer->options().magFilter().get() : osg::Texture::LINEAR;
    osg::Texture::FilterMode minFilter =
        layer ? layer->options().minFilter().get() : osg::Texture::LINEAR;

    tex->setFilter( osg::Texture::MAG_FILTER, magFilter );
    tex->setFilter( osg::Texture::MIN_FILTER, minFilter );
    tex->setMaxAnisotropy( 4.0f );

    // Disable mip mapping for npot tiles
    if (!ImageUtils::isPowerOfTwo( image ) || (!image->isMipmap() && ImageUtils::isCompressed(image)))
    {
        tex->setFilter( osg::Texture::MIN_FILTER, osg::Texture::LINEAR );
    }

    ImageUtils::activateMipMaps(tex);

    return tex;
}

osg::Texture*
TerrainTileModelFactory::createCoverageTexture(osg::Image*       image,
                                               const ImageLayer* layer) const
{
    osg::Texture2D* tex = new osg::Texture2D( image );

    tex->setWrap( osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE );
    tex->setWrap( osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE );
    tex->setResizeNonPowerOfTwoHint(false);

    tex->setFilter( osg::Texture::MAG_FILTER, osg::Texture::NEAREST );
    tex->setFilter( osg::Texture::MIN_FILTER, osg::Texture::NEAREST );

    tex->setMaxAnisotropy( 1.0f );

    return tex;
}

osg::Texture*
TerrainTileModelFactory::createElevationTexture(osg::Image* image) const
{
    osg::Texture2D* tex = new osg::Texture2D( image );
    tex->setInternalFormat(GL_R32F);
    tex->setFilter( osg::Texture::MAG_FILTER, osg::Texture::LINEAR );
    tex->setFilter( osg::Texture::MIN_FILTER, osg::Texture::NEAREST );
    tex->setWrap  ( osg::Texture::WRAP_S,     osg::Texture::CLAMP_TO_EDGE );
    tex->setWrap  ( osg::Texture::WRAP_T,     osg::Texture::CLAMP_TO_EDGE );
    tex->setResizeNonPowerOfTwoHint( false );
    tex->setMaxAnisotropy( 1.0f );
    return tex;
}

osg::Texture*
TerrainTileModelFactory::createNormalTexture(osg::Image* image) const
{
    osg::Texture2D* tex = new osg::Texture2D( image );
    tex->setInternalFormatMode(osg::Texture::USE_IMAGE_DATA_FORMAT);
    tex->setFilter( osg::Texture::MAG_FILTER, osg::Texture::LINEAR );
    tex->setFilter( osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR );
    tex->setWrap  ( osg::Texture::WRAP_S,     osg::Texture::CLAMP_TO_EDGE );
    tex->setWrap  ( osg::Texture::WRAP_T,     osg::Texture::CLAMP_TO_EDGE );
    tex->setResizeNonPowerOfTwoHint( false );
    tex->setMaxAnisotropy( 1.0f );
    return tex;
}
