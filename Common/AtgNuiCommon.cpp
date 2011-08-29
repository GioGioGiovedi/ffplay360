//--------------------------------------------------------------------------------------
// AtgNuiCommon.cpp
//
// Common defines and macros for NUI samples 
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "stdafx.h"
#include "AtgNuiCommon.h"

namespace ATG
{
    //--------------------------------------------------------------------------------------
    // Outputs verbose error message from NUI errors
    //--------------------------------------------------------------------------------------
    VOID NuiPrintError( HRESULT hResult, const CHAR* szFunctionName )
    {
        switch (hResult)
        {
        case E_INVALIDARG:
            ATG_PrintError( "%s failed with E_INVALIDARG\n", szFunctionName );
            break;

        case E_NUI_ALREADY_INITIALIZED:
            ATG_PrintError( "%s failed with E_NUI_ALREADY_INITIALIZED\n", szFunctionName );
            break;

        case E_NUI_DATABASE_NOT_FOUND:
            ATG_PrintError( "%s failed with E_NUI_DATABASE_NOT_FOUND\n", szFunctionName );
            break;

        case E_NUI_DATABASE_VERSION_MISMATCH:
            ATG_PrintError( "%s failed with E_NUI_DATABASE_VERSION_MISMATCH\n", szFunctionName );
            break;

        case E_OUTOFMEMORY:
            ATG_PrintError( "%s failed with E_OUTOFMEMORY\n", szFunctionName );
            break;

        case E_NUI_DEVICE_NOT_READY:
            ATG_PrintError( "%s failed with E_NUI_DEVICE_NOT_READY\n", szFunctionName );
            break;

        case E_NUI_DEVICE_NOT_CONNECTED:
            ATG_PrintError( "%s failed with E_NUI_DEVICE_NOT_CONNECTED\n", szFunctionName );
            break;

        case E_NUI_FEATURE_NOT_INITIALIZED:
            ATG_PrintError( "%s failed with E_NUI_FEATURE_NOT_INITIALIZED\n", szFunctionName );
            break;

        case E_NUI_IMAGE_STREAM_IN_USE:
            ATG_PrintError( "%s failed with E_NUI_IMAGE_STREAM_IN_USE\n", szFunctionName );
            break;

        default:
            ATG_PrintError( "%s failed with 0x%x\n", szFunctionName, (UINT)hResult );
        }
    }

    //--------------------------------------------------------------------------------------
    // Applies tilt correction to the skeleton data data. Source and destination can be the same
    //--------------------------------------------------------------------------------------
    VOID ApplyTiltCorrectionInPlayerSpace( NUI_SKELETON_FRAME* pDstSkeleton, const NUI_SKELETON_FRAME* pSrcSkeleton )
    {
        assert( pDstSkeleton );
        assert( pSrcSkeleton );

        if ( !pDstSkeleton ||
             !pSrcSkeleton )
        {
            return;
        }

        static const XMVECTOR vUp = XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
        static XMVECTOR vAverageSpine[ NUI_SKELETON_COUNT ] = { XMVectorZero(), XMVectorZero(), XMVectorZero(),
                                                                XMVectorZero(), XMVectorZero(), XMVectorZero() };
        static XMVECTOR vAverageNormalToGravity = pSrcSkeleton->vNormalToGravity;

        // Get a valid up vector
        XMVECTOR vNormToGrav = pSrcSkeleton->vNormalToGravity;

        // Check for an invalid up vector (we will synthesize it from
        // the floor plane if that data is present). If we can't get an up
        // vector, we default to 0.0, 1.0, 0.0 instead.
        if ( fabs(vNormToGrav.x) < FLT_EPSILON &&
             fabs(vNormToGrav.y) < FLT_EPSILON &&
             fabs(vNormToGrav.z) < FLT_EPSILON )
        {
            vNormToGrav = vUp;
        }

        // Calculate running average of vector
        vAverageNormalToGravity = XMVectorLerp( vAverageNormalToGravity, vNormToGrav, 0.1f );

        // Generate the leveling matrix and apply it to all points on any skeletons
        // which are currently being tracked. 
        XMMATRIX matLevel = NuiTransformMatrixLevel( vAverageNormalToGravity );

        for ( INT i = 0 ; i < NUI_SKELETON_COUNT; i++ )
        {
            const NUI_SKELETON_DATA* pSkeletonData = &pSrcSkeleton->SkeletonData[ i ];
            XMVECTOR vSpine = pSkeletonData->SkeletonPositions[ NUI_SKELETON_POSITION_SPINE ];

            if ( pSkeletonData->eTrackingState != NUI_SKELETON_TRACKED )
            {
                vAverageSpine[ i ] = XMVectorZero();
                continue;
            }
            else
            {
                UINT uCompareResults;
                XMVectorEqualR( &uCompareResults, XMVectorZero(), vAverageSpine[ i ] );

                // if still set to zero, then start the running average
                if( XMComparisonAllTrue( uCompareResults ) )
                {
                    vAverageSpine[ i ] = vSpine;
                }
            }

            // Running average of spine
            vAverageSpine[ i ] = XMVectorLerp( vAverageSpine[ i ], vSpine, 0.1f );

            XMFLOAT4 fAverageSpine;
            XMStoreFloat4( &fAverageSpine, vAverageSpine[ i ] );
            XMMATRIX matTranslateToOrigin = XMMatrixTranslation( -fAverageSpine.x, 0, -fAverageSpine.z );
            XMMATRIX matTranslateFromOrigin = XMMatrixTranslation( fAverageSpine.x, 0, fAverageSpine.z );
            XMMATRIX matTransformation = matTranslateToOrigin * matLevel * matTranslateFromOrigin;

            for ( UINT j = 0; j < NUI_SKELETON_POSITION_COUNT; j++ )
            {
                pDstSkeleton->SkeletonData[ i ].SkeletonPositions[ j ] = XMVector3Transform( pSkeletonData->SkeletonPositions[ j ], matLevel );
            }
        }
    }
};


