//--------------------------------------------------------------------------------------
//
// XMV.cpp
//
// Sample showing the how to play XMV movies with the RenderNextFrame method, play
// from memory, and rotate and scale the movie while it is playing.
//
// Xbox Advanced Technology Group.
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include <xtl.h>
#include <xboxmath.h>
#include <AtgApp.h>
#include <AtgFont.h>
#include <AtgHelp.h>
#include <AtgInput.h>
#include <AtgResource.h>
#include <AtgUtil.h>
#include <xaudio2.h>
#include <xmedia2.h>
#include "Tools\xmv.h"
//static const char* filename;

const char* g_strMovieName;// = filename; //"game:\\Media\\Video\\Sample.wmv";

// Get global access to the main D3D device
extern D3DDevice*   g_pd3dDevice;



//--------------------------------------------------------------------------------------
// Callouts for labelling the gamepad on the help screen
//--------------------------------------------------------------------------------------
ATG::HELP_CALLOUT g_HelpCallouts[] =
{
    { ATG::HELP_BACK_BUTTON, ATG::HELP_PLACEMENT_2, L"Display\nhelp" },
    { ATG::HELP_A_BUTTON, ATG::HELP_PLACEMENT_2, L"Play/Pause\nmovie" },
    { ATG::HELP_B_BUTTON, ATG::HELP_PLACEMENT_1, L"Stop movie" },
    { ATG::HELP_LEFT_STICK, ATG::HELP_PLACEMENT_1, L"Scale movie" },
    { ATG::HELP_RIGHT_STICK, ATG::HELP_PLACEMENT_1, L"Rotate movie" },
};
static const DWORD  NUM_HELP_CALLOUTS = ARRAYSIZE( g_HelpCallouts );


//--------------------------------------------------------------------------------------
// Name: class Sample
// Desc: Main class to run this application. Most functionality is inherited
//       from the ATG::Application base class.
//--------------------------------------------------------------------------------------
class Sample : public ATG::Application
{
    // Pointer to XMV player object.
    IXMedia2XmvPlayer* m_xmvPlayer;
    // Structure for controlling where the movie is played.
    XMEDIA_VIDEO_SCREEN m_videoScreen;
    // Parameters to control scaling and rotation of video.
    float m_angle;
    float m_xScale;
    float m_yScale;
    // Tell XMV player about scaling and rotation parameters.
 	void XMVMain(const char* filename);
	void InitVideoScreen();
    // Buffer for holding XMV data when playing from memory.
    void* m_movieBuffer;
    // XAudio2 object.
    IXAudio2* m_pXAudio2;

    ATG::Timer m_Timer;
    ATG::Font m_Font;
    ATG::Help m_Help;
    BOOL m_bDrawHelp;

    BOOL m_bFailed;

private:
    virtual HRESULT Initialize();
    virtual HRESULT Update();
    virtual HRESULT Render();
};

//--------------------------------------------------------------------------------------
// Name: Initialize()
// Desc: This creates all device-dependent display objects.
//--------------------------------------------------------------------------------------
HRESULT Sample::Initialize()
{
    m_xmvPlayer = 0;
    m_movieBuffer = 0;

    // Initialize the XAudio2 Engine. The XAudio2 Engine is needed for movie playback.
    UINT32 flags = 0;
#ifdef _DEBUG
    flags |= XAUDIO2_DEBUG_ENGINE;
#endif

    HRESULT hr = XAudio2Create( &m_pXAudio2, flags );
    if( FAILED( hr ) )
        ATG::FatalError( "Error %#X calling XAudio2Create\n", hr );

    IXAudio2MasteringVoice* pMasteringVoice = NULL;
    hr = m_pXAudio2->CreateMasteringVoice( &pMasteringVoice );
    if( FAILED( hr ) )
        ATG::FatalError( "Error %#X calling CreateMasteringVoice\n", hr );

    // Create the font
    /*if( FAILED( m_Font.Create( "game:\\Media\\Fonts\\Arial_16.xpr" ) ) )
        return ATGAPPERR_MEDIANOTFOUND;*/

    // Confine text drawing to the title safe area
    //m_Font.SetWindow( ATG::GetTitleSafeArea() );

    // Create the help
   /* m_bDrawHelp = FALSE;
    if( FAILED( m_Help.Create( "game:\\Media\\Help\\Help.xpr" ) ) )
        return ATGAPPERR_MEDIANOTFOUND;*/

    m_bFailed = FALSE;

    return S_OK;
}


static Sample atgApp;
//--------------------------------------------------------------------------------------
// Name: main()
// Desc: Entry point to the program
//--------------------------------------------------------------------------------------
void XMVMain(const char* filename)
{
    g_strMovieName = filename;
	//printf("Passed to xmv player filename - %s\n", filename);
    // For movie playback we want to synchronize to the monitor.
    atgApp.m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    ATG::GetVideoSettings( &atgApp.m_d3dpp.BackBufferWidth, &atgApp.m_d3dpp.BackBufferHeight );
    atgApp.Run();
}

void SetD3DDevice(D3DDevice* device) {
	atgApp.SetD3DDevice((ATG::D3DDevice*)device);
}


//--------------------------------------------------------------------------------------
// Name: InitVideoScreen()
// Desc: Adjust how the movie is displayed on the screen. Horizontal and vertical
//      scaling and rotation are applied.
//--------------------------------------------------------------------------------------
VOID Sample::InitVideoScreen()
{
    const int width = m_d3dpp.BackBufferWidth/20;
    const int height = m_d3dpp.BackBufferHeight/15;
    const int hWidth = width / 2;
    const int hHeight = height / 2;

    // Scale the output width.
    float left = 0;
    float right = width*10;
    float top = 0;
    float bottom = height*10;
/**/
    float cosTheta = cos( m_angle );
    float sinTheta = sin( m_angle );

    // Apply the scaling and rotation.
    m_videoScreen.aVertices[ 0 ].fX = 0; //hWidth + ( left * cosTheta - top * sinTheta );
    m_videoScreen.aVertices[ 0 ].fY = 0; //hHeight + ( top * cosTheta + left * sinTheta );
    m_videoScreen.aVertices[ 0 ].fZ = 0;

    m_videoScreen.aVertices[ 1 ].fX = right; //hWidth + ( right * cosTheta - top * sinTheta );
    m_videoScreen.aVertices[ 1 ].fY = 0; //hHeight + ( top * cosTheta + right * sinTheta );
    m_videoScreen.aVertices[ 1 ].fZ = 0;

    m_videoScreen.aVertices[ 2 ].fX = 0; //hWidth + ( left * cosTheta - bottom * sinTheta );
    m_videoScreen.aVertices[ 2 ].fY = bottom; //m_d3dpp.BackBufferHeight; //hHeight + ( bottom * cosTheta + left * sinTheta );
    m_videoScreen.aVertices[ 2 ].fZ = 0;

    m_videoScreen.aVertices[ 3 ].fX = right; //hWidth + ( right * cosTheta - bottom * sinTheta );
    m_videoScreen.aVertices[ 3 ].fY = bottom; //m_d3dpp.BackBufferHeight; //hHeight + ( bottom * cosTheta + right * sinTheta );
    m_videoScreen.aVertices[ 3 ].fZ = 0;

    // Always leave the UV coordinates at the default values.
    m_videoScreen.aVertices[ 0 ].fTu = 0;
    m_videoScreen.aVertices[ 0 ].fTv = 0;
    m_videoScreen.aVertices[ 1 ].fTu = 1;
    m_videoScreen.aVertices[ 1 ].fTv = 0;
    m_videoScreen.aVertices[ 2 ].fTu = 0;
    m_videoScreen.aVertices[ 2 ].fTv = 1;
    m_videoScreen.aVertices[ 3 ].fTu = 1;
    m_videoScreen.aVertices[ 3 ].fTv = 1;

    // Tell the XMV player to use the new settings.
    // This locks the vertex buffer so it may cause stalls if called every frame.
    m_xmvPlayer->SetVideoScreen( &m_videoScreen );
}


//--------------------------------------------------------------------------------------
// Name: Update()
// Desc: Called once per frame, the call is the entry point for animating the scene.
//       The movie is played from here.
//--------------------------------------------------------------------------------------
HRESULT Sample::Update()
{
    // Get the current gamepad state
    ATG::GAMEPAD* pGamepad = ATG::Input::GetMergedInput();

    if( m_xmvPlayer )
    {
        // Process movie-playing controls

        // 'A' means pause or unpause the movie.
        if( pGamepad->wPressedButtons & XINPUT_GAMEPAD_A )
        {
            XMEDIA_PLAYBACK_STATUS playbackStatus;

            if( SUCCEEDED( m_xmvPlayer->GetStatus( &playbackStatus ) ) )
            {
                if( XMEDIA_PLAYER_PAUSED == playbackStatus.Status )
                {
                    m_xmvPlayer->Resume();
                }
                else
                {
                    m_xmvPlayer->Pause();
                }
            }
        }

        // 'B' means cancel the movie.
        if( pGamepad->wPressedButtons & XINPUT_GAMEPAD_B )
        {
            m_xmvPlayer->Stop( XMEDIA_STOP_IMMEDIATE );
        }

        const float scaleDown = 0.07f;

        // Let the user scale the movie while it is playing.
        //m_xScale *= ( ( pGamepad->fX1 * scaleDown ) + 1.0f );
        //m_yScale *= ( ( -pGamepad->fY1 * scaleDown ) + 1.0f );
        m_xScale *= ( ( 0.0 * scaleDown ) + 1.0f );
        m_yScale *= ( ( 0.0 * scaleDown ) + 1.0f );

        // Let the user rotate the movie while it is playing.

        // In order to allow specifying all angles don't use the processed
        // fX2 and fY2 values. These have been dead-zone processed in a
        // simplified way which prevents angles near multiples of 90 degrees
        // from being generated. Instead use the raw input summed input values
        // and do custom dead-zone checking to give a better rotation
        // experience.
        //if( pGamepad->sThumbRX || pGamepad->sThumbRY )
        {
            //m_angle = atan2( ( float )-pGamepad->sThumbRY, ( float )pGamepad->sThumbRX );
        }
            m_angle = atan2( ( float )-0.0, ( float )0.0 );

        InitVideoScreen();
    }
    else
    {
        XMEDIA_XMV_CREATE_PARAMETERS XmvParams;

        ZeroMemory( &XmvParams, sizeof( XmvParams ) );

        // Use the default audio and video streams.
        // If using a wmv file with multiple audio or video streams
        // (such as different audio streams for different languages)
        // the dwAudioStreamId & dwVideoStreamId parameters can be used 
        // to select which audio (or video) stream will be played back

        XmvParams.dwAudioStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;
        XmvParams.dwVideoStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;

        // Process input for when the movie isn't playing.
        // Set the parameters to load the movie from a file.
        XmvParams.createType = XMEDIA_CREATE_FROM_FILE;
        XmvParams.createFromFile.szFileName = g_strMovieName;
        // Additional fields can be set to control how file IO is done.

        HRESULT hr = XMedia2CreateXmvPlayer( m_pd3dDevice, m_pXAudio2, &XmvParams, &m_xmvPlayer );
        if( SUCCEEDED( hr ) )
        {
            m_angle = 0.0;
            m_xScale = 1.0;
            m_yScale = 1.0;
            InitVideoScreen();
        }
        else
        {
            m_bFailed = TRUE;
        }
    }

    // Process common input--showing help while the movie is playing

    // Toggle help
    if( pGamepad->wPressedButtons & XINPUT_GAMEPAD_BACK )
        m_bDrawHelp = !m_bDrawHelp;

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Name: Render()
// Desc: Sets up render states, clears the viewport, and renders the scene.
//--------------------------------------------------------------------------------------
HRESULT Sample::Render()
{
    // Draw a gradient filled background
    ATG::RenderBackground( 0xff0000ff, 0xff000000 );

    // If we are currently playing a movie.
    if( m_xmvPlayer )
    {
        // If RenderNextFrame does not return S_OK then the frame was not
        // rendered (perhaps because it was cancelled) so a regular frame
        // buffer should be rendered before calling present.
        HRESULT hr = m_xmvPlayer->RenderNextFrame( 0, NULL );

        // Reset our cached view of what pixel and vertex shaders are set, because
        // it is no longer accurate, since XMV will have set their own shaders.
        // This avoids problems when the shader cache thinks it knows what shader
        // is set and it is wrong.
        m_pd3dDevice->SetVertexShader( 0 );
        m_pd3dDevice->SetPixelShader( 0 );
        m_pd3dDevice->SetVertexDeclaration( 0 );

        if( FAILED( hr ) || hr == ( HRESULT )XMEDIA_W_EOF )
        {
            // Release the movie object
            m_xmvPlayer->Release();
            m_xmvPlayer = 0;
            // Movie playback changes various D3D states, so you should reset the
            // states that you need after movie playback is finished.
            m_pd3dDevice->SetRenderState( D3DRS_VIEWPORTENABLE, TRUE );

            // Free up any memory allocated for playing from memory.
            if( m_movieBuffer )
            {
                free( m_movieBuffer );
                m_movieBuffer = 0;
            }
        }

        // Fall through to the regular display, which we overlay on the movie
        // as appropriate.
    }

    // Show title, frame rate, and help
    m_Timer.MarkFrame();
    /*if( m_bDrawHelp )
    {
        m_Help.Render( &m_Font, g_HelpCallouts, NUM_HELP_CALLOUTS );
    }
    else
    {
        m_Font.Begin();
        m_Font.SetScaleFactors( 1.2f, 1.2f );
        m_Font.DrawText( 0, 0, 0xffffffff, L"XMV" );
        m_Font.SetScaleFactors( 1.0f, 1.0f );
        m_Font.DrawText( -1, 0, 0xffffff00, m_Timer.GetFrameRate(), ATGFONT_RIGHT );

        m_Font.SetScaleFactors( 1.2f, 1.2f );

        if( m_xmvPlayer )
        {
            m_Font.DrawText( 0, 70, 0xffffffff, L"Press " GLYPH_A_BUTTON L" to pause.\n"
                                                L"Press " GLYPH_B_BUTTON L" to stop.\n"
                                                L"Use the thumbsticks to scale and rotate." );
        }
        else
        {
            WCHAR strBuffer[1000];
            if( m_bFailed )
                swprintf_s( strBuffer, L"Failed to load movie\n%S", g_strMovieName );
            else
                swprintf_s( strBuffer, L"Press " GLYPH_A_BUTTON L"to play\n%S", g_strMovieName );
            m_Font.DrawText( 0, 70, 0xffffffff, strBuffer );

            m_Font.DrawText( 0, 160, 0xffffffff, L"Press " GLYPH_Y_BUTTON L"to play from memory" );
        }

        m_Font.End();
    }*/

    // Present the scene
    m_pd3dDevice->Present( NULL, NULL, NULL, NULL );

    return S_OK;
}
