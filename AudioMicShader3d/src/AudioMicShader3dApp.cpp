/*
 Copyright (c) 2014, Paul Houx - All rights reserved.
 This code is intended for use with the Cinder C++ library: http://libcinder.org
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and
	the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
	the following disclaimer in the documentation and/or other materials provided with the distribution.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Texture.h"
#include "cinder/gl/Vbo.h"
#include "cinder/Camera.h"
#include "cinder/Channel.h"
#include "cinder/ImageIo.h"
#include "cinder/MayaCamUI.h"
#include "cinder/Rand.h"
#include "cinder/audio/MonitorNode.h"
#include "cinder/audio/Device.h"
#include "cinderSyphon.h"
#include "MeshHelper.h"
#include "Resources.h"
#include "FMOD.hpp"

// Channel callback function used by FMOD to notify us of channel events
FMOD_RESULT F_CALLBACK channelCallback(FMOD_CHANNEL *channel, FMOD_CHANNEL_CALLBACKTYPE type, void *commanddata1, void *commanddata2);
#define INPUT_DEVICE "Scarlett 2i2 USB"

using namespace ci;
using namespace ci::app;
using namespace std;

class AudioVisualizerApp : public AppNative {
public:
    void prepareSettings( Settings* settings );
    
    void setup();
    void shutdown();
    void update();
    void draw();
    
    void mouseDown( MouseEvent event );
    void mouseDrag( MouseEvent event );
    void mouseUp( MouseEvent event );
    void keyDown( KeyEvent event );
    void resize();

    // play the audio file
    void		playAudio( const fs::path& file );
    
private:
    // width and height of our mesh
    static const int kWidth = 512;
    static const int kHeight = 512;
    
    // number of frequency bands of our spectrum
    static const int kBands = 1024;
    static const int kHistory = 128;
    
    Channel32f			mChannelLeft;
    Channel32f			mChannelRight;
    CameraPersp			mCamera;
    MayaCamUI			mMayaCam;
    gl::GlslProg		mShader;
    gl::TextureRef			mTextureLeft;
    gl::TextureRef		mTextureRight;
    gl::Texture::Format	mTextureFormat;
    gl::VboMesh			mMesh;
    	ci::gl::VboMesh				mIcosahedron;
    uint32_t			mOffset;
    
//    FMOD::System*		mFMODSystem;
//    FMOD::Sound*		mFMODSound;
//    FMOD::Channel*		mFMODChannel;
    
    bool				mIsMouseDown;
    bool				mIsAudioPlaying;
    double				mMouseUpTime;
    double				mMouseUpDelay;
    
    vector<string>		mAudioExtensions;
    fs::path			mAudioPath;
    
    audio::InputDeviceNodeRef		mInputDeviceNode;
    audio::MonitorSpectralNodeRef	mMonitorSpectralNode;
    vector<float>					mMagSpectrum;

	syphonServer mTextureSyphon;
    
public:
    bool				signalChannelEnd;
};

void AudioVisualizerApp::prepareSettings(Settings* settings)
{
    settings->setFullScreen(false);
    settings->setWindowSize(1280, 720);
}

void AudioVisualizerApp::setup()
{
    auto ctx = audio::Context::master();
    std::cout << "Devices available: " << endl;
    for( const auto &dev : audio::Device::getInputDevices() ) {
        std::cout<<dev->getName() <<endl;
    }
    
    std::vector<audio::DeviceRef> devices = audio::Device::getInputDevices();
    const auto dev = audio::Device::findDeviceByName(INPUT_DEVICE);
    if (!dev){
        cout<<"Could not find " << INPUT_DEVICE << endl;
        mInputDeviceNode = ctx->createInputDeviceNode();
        cout<<"Using default input"<<endl;
    } else {
        mInputDeviceNode = ctx->createInputDeviceNode(dev);
    }
    
    // By providing an FFT size double that of the window size, we 'zero-pad' the analysis data, which gives
    // an increase in resolution of the resulting spectrum data.
    auto monitorFormat = audio::MonitorSpectralNode::Format().fftSize( 2048 ).windowSize( 1024 );
    mMonitorSpectralNode = ctx->makeNode( new audio::MonitorSpectralNode( monitorFormat ) );
    
    mInputDeviceNode >> mMonitorSpectralNode;
    
    // InputDeviceNode (and all InputNode subclasses) need to be enabled()'s to process audio. So does the Context:
    mInputDeviceNode->enable();
    ctx->enable();
    
    getWindow()->setTitle( mInputDeviceNode->getDevice()->getName() );
    //////

    // initialize signals
    signalChannelEnd = false;

    mIsAudioPlaying = false;
    
    // setup camera
    mCamera.setPerspective(50.0f, 1.0f, 1.0f, 10000.0f);
    mCamera.setEyePoint( Vec3f(-kWidth/2, kHeight/2, -kWidth/8) );
    mCamera.setCenterOfInterestPoint( Vec3f(kWidth/4, -kHeight/8, kWidth/4) );
    
    // create channels from which we can construct our textures
    mChannelLeft = Channel32f(kBands, kHistory);
    mChannelRight = Channel32f(kBands, kHistory);
    memset(	mChannelLeft.getData(), 0, mChannelLeft.getRowBytes() * kHistory );
    memset(	mChannelRight.getData(), 0, mChannelRight.getRowBytes() * kHistory );
    
    // create texture format (wrap the y-axis, clamp the x-axis)
    mTextureFormat.setWrapS( GL_CLAMP );
    mTextureFormat.setWrapT( GL_REPEAT );
    mTextureFormat.setMinFilter( GL_LINEAR );
    mTextureFormat.setMagFilter( GL_LINEAR );
    
    // compile shader
    try {
        mShader = gl::GlslProg( loadResource( GLSL_VERT ), loadResource( GLSL_FRAG ) );
    }
    catch( const std::exception& e ) {
        console() << e.what() << std::endl;
        quit();
        return;
    }
    
    // create static mesh (all animation is done in the vertex shader)
    std::vector<Vec3f>      vertices;
    std::vector<Colorf>     colors;
    std::vector<Vec2f>      coords;
    std::vector<uint32_t>	indices;
    
    for(size_t h=0;h<kHeight;++h)
    {
        for(size_t w=0;w<kWidth;++w)
        {
            // add polygon indices
            if(h < kHeight-1 && w < kWidth-1)
            {
                size_t offset = vertices.size();
                
                indices.push_back(offset);
                indices.push_back(offset+kWidth);
                indices.push_back(offset+kWidth+1);
                indices.push_back(offset);
                indices.push_back(offset+kWidth+1);
                indices.push_back(offset+1);
            }
            
            // add vertex
            vertices.push_back( Vec3f(float(w), 0, float(h)) );
            
            // add texture coordinates
            // note: we only want to draw the lower part of the frequency bands,
            //  so we scale the coordinates a bit
            const float part = 0.5f;
            float s = w / float(kWidth-1);
            float t = h / float(kHeight-1);
            coords.push_back( Vec2f(part - part * s, t) );
            
            // add vertex colors
            colors.push_back( Color(CM_HSV, s, 0.5f, 0.75f) );
        }
    }
    
    gl::VboMesh::Layout layout;
    layout.setStaticPositions();
    layout.setStaticColorsRGB();
    layout.setStaticIndices();
    layout.setStaticTexCoords2d();
    
    mMesh = gl::VboMesh(vertices.size(), indices.size(), layout, GL_TRIANGLES);
    mMesh.bufferPositions(vertices);
    mMesh.bufferColorsRGB(colors);
    mMesh.bufferIndices(indices);
    mMesh.bufferTexCoords2d(0, coords);

    
    ////////////////
    // Declare vectors

    vector<Vec3f> normals;
    vector<Vec3f> positions;
    vector<Vec2f> texCoords;
    std::vector<Colorf>     clrs;
    Vec2f mResolution(kWidth*.035f,kHeight*.035f);
    // Mesh dimensions
    float halfHeight	= (float)mResolution.x * 0.5f;
    float halfWidth		= (float)mResolution.y * 0.5f;
    float unit			= 3.0f / (float)mResolution.x;
    Vec3f scale( unit, 0.5f, unit );
    scale *= 100.;
    Vec3f offset( halfHeight, 0.f, halfWidth );
    
    // Iterate through rows and columns using segment count
    for ( int32_t y = 0; y < mResolution.y; y++ ) {
        for ( int32_t x = 0; x < mResolution.x; x++ ) {
            
            // Set texture coordinate in [ 0 - 1, 0 - 1 ] range
            Vec2f texCoord( (float)x / (float)mResolution.x, (float)y / (float)mResolution.y );
            texCoords.push_back( texCoord );
            
            // Use random value for Y position
            float value = randFloat();
            
            // Set vertex position
            Vec3f position( (float)x - halfWidth, value, (float)y - halfHeight );
            positions.push_back( position * scale + offset );
            
            // Add a default normal for now (we'll calculate this down below)
            normals.push_back( Vec3f::zero() );
            
            // Add indices to form quad from two triangles
            int32_t xn = x + 1 >= mResolution.x ? 0 : 1;
            int32_t yn = y + 1 >= mResolution.y ? 0 : 1;
            indices.push_back( x + mResolution.x * y );
            indices.push_back( ( x + xn ) + mResolution.x * y);
            indices.push_back( ( x + xn ) + mResolution.x * ( y + yn ) );
            indices.push_back( x + mResolution.x * ( y + yn ) );
            indices.push_back( ( x + xn ) + mResolution.x * ( y + yn ) );
            indices.push_back( x + mResolution.x * y );
            
            float s = x / float(kWidth-1);
            float t = y / float(kHeight-1);
            
            // add vertex colors
            clrs.push_back( Color(CM_HSV, s, 0.5f, 0.75f) );
        }
    }
    
    // Iterate through again to set normals
    for ( int32_t y = 0; y < mResolution.y - 1; y++ ) {
        for ( int32_t x = 0; x < mResolution.x - 1; x++ ) {
            Vec3f vert0 = positions[ indices[ ( x + mResolution.x * y ) * 6 ] ];
            Vec3f vert1 = positions[ indices[ ( ( x + 1 ) + mResolution.x * y ) * 6 ] ];
            Vec3f vert2 = positions[ indices[ ( ( x + 1 ) + mResolution.x * ( y + 1 ) ) * 6 ] ];
            normals[ x + mResolution.x * y ] = Vec3f( ( vert1 - vert0 ).cross( vert1 - vert2 ).normalized() );
        }
    }
    
    // Use the MeshHelper to create a VboMesh from our vectors
    mIcosahedron = gl::VboMesh( MeshHelper::create( indices, positions, normals, texCoords ) );
        //mIcosahedron.bufferColorsRGB(clrs);
    //////////////////////
    
    // play audio using the Cinder FMOD block
//    FMOD::System_Create( &mFMODSystem );


    //mFMODSystem->init( 32, FMOD_INIT_NORMAL | FMOD_INIT_ENABLE_PROFILE, NULL );
//    if ( mFMODSystem->init( 32, FMOD_INIT_NORMAL, 0 ) != FMOD_OK ){
//        console() << "Unable to initialize system. deviceid " << endl;
//    }
//    mFMODSystem->setDriver(0);
//    
//    mFMODSystem->getRecordNumDrivers(&numdrivers);
//    
//    for (int count=0; count < numdrivers; count++)
//    {
//        char name[256];
//        
//        mFMODSystem->getRecordDriverInfo(count, name, 256, 0);
//
//        
//        printf("%d : %s\n", count + 1, name);
//    }
//    
//    int mDeviceID = 0;
//    if(mDeviceID != -1)
//        mFMODSystem->setDriver(mDeviceID);
//    if ( mFMODSystem->init( 32, FMOD_INIT_NORMAL, 0 ) != FMOD_OK ) {
//        console() << "Unable to initialize system. deviceid " << mDeviceID << endl;
//    }
    
    
//    mFMODSound = nullptr;
//    mFMODChannel = nullptr;
    
//    playAudio( findAudio( mAudioPath ) );
    
    mIsMouseDown = false;
    mMouseUpDelay = 5.0;
    mMouseUpTime = getElapsedSeconds() - mMouseUpDelay;
    
    // the texture offset has two purposes:
    //  1) it tells us where to upload the next spectrum data
    //  2) we use it to offset the texture coordinates in the shader for the scrolling effect
    mOffset = 0;
    
    mTextureSyphon.setName("Mic3d");
}

void AudioVisualizerApp::shutdown()
{
    
//    if(mFMODSystem)
//    mFMODSystem->release();
}

void AudioVisualizerApp::update()
{
    
    mMagSpectrum = mMonitorSpectralNode->getMagSpectrum();
    // update FMOD so it can notify us of events
//    mFMODSystem->update();
    
    // reset FMOD signals
    signalChannelEnd= false;
    
    // get spectrum for left and right channels and copy it into our channels
    float* pDataLeft = mChannelLeft.getData() + kBands * mOffset;
    float* pDataRight = mChannelRight.getData() + kBands * mOffset;
    
    std::copy(mMagSpectrum.begin(), mMagSpectrum.end(), pDataLeft);
    std::copy(mMagSpectrum.begin(), mMagSpectrum.end(), pDataRight);
    
//    mFMODSystem->getSpectrum( pDataLeft, kBands, 0, FMOD_DSP_FFT_WINDOW_HANNING );
//    mFMODSystem->getSpectrum( pDataRight, kBands, 1, FMOD_DSP_FFT_WINDOW_HANNING );
    
    // increment texture offset
    mOffset = (mOffset+1) % kHistory;
    
    // clear the spectrum for this row to avoid old data from showing up
    pDataLeft = mChannelLeft.getData() + kBands * mOffset;
    pDataRight = mChannelRight.getData() + kBands * mOffset;
    memset( pDataLeft, 0, kBands * sizeof(float) );
    memset( pDataRight, 0, kBands * sizeof(float) );
    
    // animate camera if mouse has not been down for more than 30 seconds
    if(true || !mIsMouseDown && (getElapsedSeconds() - mMouseUpTime) > mMouseUpDelay)
    {
        float t = float( getElapsedSeconds() );
        float x = 0.5f * math<float>::cos( t * 0.07f );
        float y = 0.5f * math<float>::sin( t * 0.09f );//0.1f - 0.2f * math<float>::sin( t * 0.09f );
        float z = 0.05f * math<float>::sin( t * 0.05f ) - 0.15f;

        Vec3f eye = Vec3f(kWidth * x, kHeight * y, kHeight * z);
        
        x = 1.0f - x;
        y = -0.5f;
        z = 0.6f + 0.2f *  math<float>::sin( t * 0.12f );
        Vec3f interest = Vec3f(kWidth * x, kHeight * y, kHeight * z);
        cout<<interest<<endl;
        
        // gradually move to eye position and center of interest
        mCamera.setEyePoint( eye.lerp(0.995f, mCamera.getEyePoint()) );
        mCamera.setCenterOfInterestPoint( interest.lerp(0.990f, mCamera.getCenterOfInterestPoint()) );
    }
}

void AudioVisualizerApp::draw()
{
    gl::clear();

    // use camera
    gl::pushMatrices();
    gl::setMatrices(mCamera);
    {
        // bind shader
        mShader.bind();
        mShader.uniform("uTexOffset", mOffset / float(kHistory));
        mShader.uniform("uLeftTex", 0);
        mShader.uniform("uRightTex", 1);
        
        // create textures from our channels and bind them
        mTextureLeft = gl::Texture::create(mChannelLeft, mTextureFormat);
        mTextureRight = gl::Texture::create(mChannelRight, mTextureFormat);
        
        mTextureLeft->enableAndBind();
        mTextureRight->bind(1);
        
        // draw mesh using additive blending
        gl::enableAdditiveBlending();
        gl::color( Color(1, 1, 1) );
        //gl::draw( mMesh );
        gl::draw(mIcosahedron);
        gl::disableAlphaBlending();
        
        // unbind textures and shader
        mTextureRight->unbind();
        mTextureLeft->unbind();
        mShader.unbind();
    }
    gl::popMatrices();

    mTextureSyphon.publishScreen();

}

void AudioVisualizerApp::mouseDown( MouseEvent event )
{
    // handle mouse down
    mIsMouseDown = true;
    
    mMayaCam.setCurrentCam(mCamera);
    mMayaCam.mouseDown( event.getPos() );
}

void AudioVisualizerApp::mouseDrag( MouseEvent event )
{
    // handle mouse drag
    mMayaCam.mouseDrag( event.getPos(), event.isLeftDown(), event.isMiddleDown(), event.isRightDown() );
    mCamera = mMayaCam.getCamera();
}

void AudioVisualizerApp::mouseUp( MouseEvent event )
{
    // handle mouse up
    mMouseUpTime = getElapsedSeconds();
    mIsMouseDown = false;
}

void AudioVisualizerApp::keyDown( KeyEvent event )
{
    // handle key down
    switch( event.getCode() )
    {
        case KeyEvent::KEY_ESCAPE:
        quit();
        break;
        case KeyEvent::KEY_F4:
        if( event.isAltDown() )
        quit();
        break;
        case KeyEvent::KEY_LEFT:

        break;
        case KeyEvent::KEY_RIGHT:
        break;
        case KeyEvent::KEY_f:
        setFullScreen( !isFullScreen() );
        break;
        case KeyEvent::KEY_o:
        break;
        case KeyEvent::KEY_p:
        playAudio( mAudioPath );
        break;
        case KeyEvent::KEY_s:

        break;
    }
}

void AudioVisualizerApp::resize()
{
    // handle resize
    mCamera.setAspectRatio( getWindowAspectRatio() );
}

void AudioVisualizerApp::playAudio(const fs::path& file)
{
//    FMOD_RESULT err;
//    
//    // ignore if this is not a file
//    if(file.empty() || !fs::is_regular_file( file ))
//    return;
//    
//    // if audio is already playing, stop it first
//    stopAudio();
//    
//    // stream the audio
//    err = mFMODSystem->createStream( file.string().c_str(), FMOD_SOFTWARE, NULL, &mFMODSound );
//    err = mFMODSystem->playSound( FMOD_CHANNEL_FREE, mFMODSound, false, &mFMODChannel );
//    
//    // we want to be notified of channel events
//    err = mFMODChannel->setCallback( channelCallback );
//    
//    // keep track of the audio file
//    mAudioPath = file;
//    mIsAudioPlaying = true;
//    
//    // 
//    console() << "Now playing:" << mAudioPath.filename().string() << std::endl;
}


// Channel callback function used by FMOD to notify us of channel events
FMOD_RESULT F_CALLBACK channelCallback(FMOD_CHANNEL *channel, FMOD_CHANNEL_CALLBACKTYPE type, void *commanddata1, void *commanddata2)
{
    // we first need access to the application instance
    AudioVisualizerApp* pApp = static_cast<AudioVisualizerApp*>( App::get() );
    
    // now handle the callback
    switch(type)
    {
        case FMOD_CHANNEL_CALLBACKTYPE_END:
        // we can't call a function directly, because we are inside the FMOD thread,
        // so let's notify the application instead by setting a boolean (which is thread safe).
        pApp->signalChannelEnd = true;
        break;
        default:
        break;
    }
    
    return FMOD_OK;
}

CINDER_APP_NATIVE( AudioVisualizerApp, RendererGl )