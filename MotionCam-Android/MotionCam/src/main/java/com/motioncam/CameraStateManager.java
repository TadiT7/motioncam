package com.motioncam;

import android.app.Activity;
import android.content.Context;
import android.graphics.Matrix;
import android.graphics.PointF;
import android.os.Vibrator;
import android.util.Log;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.appcompat.content.res.AppCompatResources;

import com.motioncam.camera.CameraManualControl;
import com.motioncam.camera.NativeCamera;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.NativeCameraMetadata;
import com.motioncam.databinding.CameraActivityBinding;

import java.util.Arrays;
import java.util.List;
import java.util.Locale;

class CameraStateManager implements NativeCamera.CameraSessionListener, GestureDetector.OnGestureListener
{
    public static final String TAG = "CameraStateManager";

    private static final int MANUAL_CONTROL_MODE_ISO = 0;
    private static final int MANUAL_CONTROL_MODE_SHUTTER_SPEED = 1;
    private static final int MANUAL_CONTROL_MODE_FOCUS = 2;

    private enum FocusMode {
        CONTINUOUS,
        USER_SELECTED,
        USER_LOCKED,
        MANUAL
    }

    private enum ExposureMode {
        AUTO,
        MANUAL
    }

    public static final int USER_SELECTED_FOCUS_CANCEL_TIME_MS = 1000;

    private final NativeCamera mActiveCamera;
    private final NativeCameraMetadata mActiveCameraMetadata;

    private final Activity mActivity;
    private final CameraActivityBinding mBinding;
    private final Settings mSettings;

    private final List<Integer> mIsoValues;
    private final List<CameraManualControl.SHUTTER_SPEED> mExposureValues;

    private NativeCamera.CameraExposureState mExposureState;
    private NativeCamera.CameraFocusState mCameraFocusState;

    private boolean mAWBLock;
    private float mFocusDistance = -1;
    private FocusMode mFocusMode = FocusMode.CONTINUOUS;
    private ExposureMode mExposureMode = ExposureMode.AUTO;
    private PointF mUserFocusPt = null;
    private long mUserFocusRequestedTimestampMs = -1;
    private boolean mUserRequestedAeAfLock = false;
    private NativeCameraBuffer.ScreenOrientation mOrientation = NativeCameraBuffer.ScreenOrientation.PORTRAIT;

    CameraStateManager(NativeCamera activeCamera,
                       NativeCameraMetadata cameraMetadata,
                       Activity activity,
                       CameraActivityBinding binding,
                       Settings settings)
    {
        mActiveCamera = activeCamera;
        mActiveCameraMetadata = cameraMetadata;
        mActivity = activity;
        mBinding = binding;
        mSettings = settings;
        mAWBLock = false;

        mIsoValues = CameraManualControl.GetIsoValuesInRange(
                mActiveCameraMetadata.isoMin,
                Math.max(CameraManualControl.ISO.ISO_3200.getIso(), mActiveCameraMetadata.isoMax));

        mExposureValues = Arrays.asList(CameraManualControl.SHUTTER_SPEED.values());
    }

    public long getExposureTime() {
        return mSettings.cameraStartupSettings.exposureTime;
    }

    public int getIso() {
        return mSettings.cameraStartupSettings.iso;
    }

    public List<Integer> getIsoValues() {
        return mIsoValues;
    }

    public List<CameraManualControl.SHUTTER_SPEED> getExposureValues() {
        return mExposureValues;
    }

    public void onExposureCompSeekBarChanged(int progress, boolean fromUser) {
        if (mActiveCamera != null && fromUser) {
            float value = progress / (float) mBinding.exposureSeekBar.getMax();

            mActiveCamera.setExposureCompensation(value);
            mActiveCamera.activateCameraSettings();
        }
    }

    public void onManualControlSettingsChanged(int progress, boolean fromUser) {
        if(mActiveCamera != null && fromUser) {
            int selectionMode = (int) mActivity.findViewById(R.id.manualControlFocus).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_FOCUS) {
                double p;

                if(progress < 100) {
                    double in = (100.0f - progress) / 100.0f;
                    p = focusToLinear(in, mActiveCameraMetadata.maxFocusDistance, mActiveCameraMetadata.minFocusDistance);
                }
                else {
                    p = 0.0;
                }

                mFocusDistance = (float) p;
                setFocusMode(FocusMode.MANUAL, null);

                // Hide focus lock
                mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE);
            }
        }
    }

    public void setAuto() {
        if(mActiveCamera == null)
            return;

        // Reset everything
        mFocusMode = FocusMode.CONTINUOUS;
        mExposureMode = ExposureMode.AUTO;
        mFocusDistance = -1;
        mUserRequestedAeAfLock = false;
        mUserFocusPt = null;

        mSettings.cameraStartupSettings.useUserExposureSettings = false;
        mSettings.cameraStartupSettings.exposureTime = 0;
        mSettings.cameraStartupSettings.iso = 0;

        mBinding.focusLockPointFrame.getDrawable().setTint(mActivity.getColor(R.color.white));
        mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE);

        mActiveCamera.setAELock(false);
        mActiveCamera.setAutoExposure();
        mActiveCamera.setAutoFocus();

        mActiveCamera.activateCameraSettings();

        hideExposureControls();
        hideFocusControls();
    }

    private double mapFunc(double x) {
        return Math.log(x);
    }

    private double inverseMapFunc(double x) {
        return Math.exp(x);
    }

    private double mapLinearFocus(double x, double min, double max) {
        return (mapFunc(x) - mapFunc(min)) / (mapFunc(max) - mapFunc(min));
    }

    private double focusToLinear(double x, double min, double max) {
        return inverseMapFunc(x * (mapFunc(max) - mapFunc(min)) + mapFunc(min));
    }

    public void toggleFocus() {
        // Hide if shown
        if(mActivity.findViewById(R.id.manualControlFocus).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) mActivity.findViewById(R.id.manualControlFocus).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_FOCUS) {
                hideFocusControls();
                return;
            }
        }

        double progress = 100.0 * mapLinearFocus(mFocusDistance, mActiveCameraMetadata.maxFocusDistance, mActiveCameraMetadata.minFocusDistance);

        ((SeekBar) mActivity.findViewById(R.id.manualControlSeekBar)).setMax(100);
        ((SeekBar) mActivity.findViewById(R.id.manualControlSeekBar)).setProgress((int)Math.round(100 - progress));
        ((SeekBar) mActivity.findViewById(R.id.manualControlSeekBar)).setTickMark(null);

        mActivity.findViewById(R.id.manualControlFocus).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_FOCUS);
        mActivity.findViewById(R.id.manualControlFocus).setVisibility(View.VISIBLE);

        ((ImageView) mActivity.findViewById(R.id.focusBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(mActivity, R.drawable.chevron_up));

        alignManualControlView(false);
    }

    public void toggleIso() {
        // Hide if shown
        if(mActivity.findViewById(R.id.manualControlExposure).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) mActivity.findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
                hideExposureControls();
                return;
            }
        }

        int isoIdx = CameraManualControl.FindISOIdx(mIsoValues, mSettings.cameraStartupSettings.iso);

        String current = String.valueOf(mIsoValues.get(isoIdx));
        String next = "-";
        String prev = "-";

        if(isoIdx > 0) {
            prev = String.valueOf(mIsoValues.get(isoIdx - 1));
        }

        if(isoIdx < mIsoValues.size() - 1)
            next = String.valueOf(mIsoValues.get(isoIdx + 1));

        ((TextView) mActivity.findViewById(R.id.manualControlMinusBtn)).setText(prev);
        ((TextView) mActivity.findViewById(R.id.manualControlCurrentValue)).setText(current);
        ((TextView) mActivity.findViewById(R.id.manualControlPlusBtn)).setText(next);

        mActivity.findViewById(R.id.manualControlExposure).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_ISO);
        mActivity.findViewById(R.id.manualControlExposure).setVisibility(View.VISIBLE);

        ((ImageView) mActivity.findViewById(R.id.isoBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(mActivity, R.drawable.chevron_up));

        alignManualControlView(false);
    }

    public void toggleShutterSpeed() {
        // Hide if shown
        if(mActivity.findViewById(R.id.manualControlExposure).getVisibility() == View.VISIBLE) {
            int selectionMode = (int) mActivity.findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

            if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
                hideExposureControls();
                return;
            }
        }

        mActivity.findViewById(R.id.manualControlExposure).setTag(R.id.manual_control_tag, MANUAL_CONTROL_MODE_SHUTTER_SPEED);
        mActivity.findViewById(R.id.manualControlExposure).setVisibility(View.VISIBLE);

        int exposureValueIdx = CameraManualControl.FindShutterSpeedIdx(mExposureValues, mSettings.cameraStartupSettings.exposureTime);

        String current = mExposureValues.get(exposureValueIdx).toString();
        String next = "-";
        String prev = "-";

        if(exposureValueIdx > 0) {
            prev = mExposureValues.get(exposureValueIdx - 1).toString();
        }

        if(exposureValueIdx < mExposureValues.size() - 1)
            next = mExposureValues.get(exposureValueIdx + 1).toString();

        ((TextView) mActivity.findViewById(R.id.manualControlMinusBtn)).setText(prev);
        ((TextView) mActivity.findViewById(R.id.manualControlCurrentValue)).setText(current);
        ((TextView) mActivity.findViewById(R.id.manualControlPlusBtn)).setText(next);

        ((ImageView) mActivity.findViewById(R.id.shutterSpeedIcon))
                .setImageDrawable(AppCompatResources.getDrawable(mActivity, R.drawable.chevron_up));

        alignManualControlView(false);
    }

    public void onManualControlPlus() {
        if(mActiveCamera == null)
            return;

        int selectionMode = (int) mActivity.findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

        if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
            int isoIdx = CameraManualControl.FindISOIdx(mIsoValues, mSettings.cameraStartupSettings.iso);
            if(isoIdx >= mIsoValues.size() - 1)
                return;

            String current = String.valueOf(mIsoValues.get(isoIdx));
            String next;
            String nextNext = "-";

            int nextIdx = isoIdx + 1;
            next = mIsoValues.get(nextIdx).toString();

            ((TextView) mActivity.findViewById(R.id.manualControlCurrentValue)).setText(next);

            if(isoIdx < mIsoValues.size() - 2) {
                int nextNextIdx = isoIdx + 2;
                nextNext = String.valueOf(mIsoValues.get(nextNextIdx));
            }

            mSettings.cameraStartupSettings.iso = mIsoValues.get(nextIdx);

            ((TextView) mActivity.findViewById(R.id.manualControlPlusBtn)).setText(nextNext);
            ((TextView) mActivity.findViewById(R.id.manualControlMinusBtn)).setText(current);
        }
        else if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
            int exposureTimeIdx = CameraManualControl.FindShutterSpeedIdx(mExposureValues, mSettings.cameraStartupSettings.exposureTime);
            if(exposureTimeIdx >= mExposureValues.size() - 1)
                return;

            String current = mExposureValues.get(exposureTimeIdx).toString();
            String next;
            String nextNext = "-";

            int nextIdx = exposureTimeIdx + 1;
            next = mExposureValues.get(nextIdx).toString();

            ((TextView) mActivity.findViewById(R.id.manualControlCurrentValue)).setText(next);

            if(exposureTimeIdx < mExposureValues.size() - 2) {
                int nextNextIdx = exposureTimeIdx + 2;
                nextNext = mExposureValues.get(nextNextIdx).toString();
            }

            mSettings.cameraStartupSettings.exposureTime = mExposureValues.get(nextIdx).getExposureTime();

            ((TextView) mActivity.findViewById(R.id.manualControlPlusBtn)).setText(nextNext);
            ((TextView) mActivity.findViewById(R.id.manualControlMinusBtn)).setText(current);
        }


        mExposureMode = ExposureMode.MANUAL;
        mSettings.cameraStartupSettings.useUserExposureSettings = true;

        mActiveCamera.setManualExposureValues(mSettings.cameraStartupSettings.iso, mSettings.cameraStartupSettings.exposureTime);
        mActiveCamera.activateCameraSettings();
    }

    public void onManualControlMinus() {
        int selectionMode = (int) mActivity.findViewById(R.id.manualControlExposure).getTag(R.id.manual_control_tag);

        if(selectionMode == MANUAL_CONTROL_MODE_ISO) {
            int isoIdx = CameraManualControl.FindISOIdx(mIsoValues, mSettings.cameraStartupSettings.iso);
            if(isoIdx == 0)
                return;

            String current = String.valueOf(mIsoValues.get(isoIdx));
            String prev;
            String prevPrev = "-";

            int prevIdx = isoIdx - 1;
            prev = String.valueOf(mIsoValues.get(prevIdx));

            ((TextView) mActivity.findViewById(R.id.manualControlCurrentValue)).setText(prev);

            if(isoIdx > 1) {
                int prevPrevIdx = isoIdx - 2;
                prevPrev = String.valueOf(mIsoValues.get(prevPrevIdx));
            }

            mSettings.cameraStartupSettings.iso = mIsoValues.get(prevIdx);

            ((TextView) mActivity.findViewById(R.id.manualControlPlusBtn)).setText(current);
            ((TextView) mActivity.findViewById(R.id.manualControlMinusBtn)).setText(prevPrev);
        }
        else if(selectionMode == MANUAL_CONTROL_MODE_SHUTTER_SPEED) {
            int exposureIdx = CameraManualControl.FindShutterSpeedIdx(mExposureValues, mSettings.cameraStartupSettings.exposureTime);
            if(exposureIdx == 0)
                return;

            String current = mExposureValues.get(exposureIdx).toString();
            String prev;
            String prevPrev = "-";

            int prevIdx = exposureIdx - 1;
            prev = mExposureValues.get(prevIdx).toString();

            ((TextView) mActivity.findViewById(R.id.manualControlCurrentValue)).setText(prev);

            if(exposureIdx > 1) {
                int prevPrevIdx = exposureIdx - 2;
                prevPrev = mExposureValues.get(prevPrevIdx).toString();
            }

            mSettings.cameraStartupSettings.exposureTime = mExposureValues.get(prevIdx).getExposureTime();

            ((TextView) mActivity.findViewById(R.id.manualControlPlusBtn)).setText(current);
            ((TextView) mActivity.findViewById(R.id.manualControlMinusBtn)).setText(prevPrev);
        }


        mExposureMode = ExposureMode.MANUAL;
        mSettings.cameraStartupSettings.useUserExposureSettings = true;

        mActiveCamera.setManualExposureValues(mSettings.cameraStartupSettings.iso, mSettings.cameraStartupSettings.exposureTime);
        mActiveCamera.activateCameraSettings();
    }

    public void hideExposureControls() {
        ((ImageView) mActivity.findViewById(R.id.isoBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(mActivity, R.drawable.chevron_down));

        ((ImageView) mActivity.findViewById(R.id.shutterSpeedIcon))
                .setImageDrawable(AppCompatResources.getDrawable(mActivity, R.drawable.chevron_down));

        mActivity.findViewById(R.id.manualControlExposure).setVisibility(View.GONE);
        alignManualControlView(false);
    }

    public void hideFocusControls() {
        ((ImageView) mActivity.findViewById(R.id.focusBtnIcon))
                .setImageDrawable(AppCompatResources.getDrawable(mActivity, R.drawable.chevron_down));

        mActivity.findViewById(R.id.manualControlFocus).setVisibility(View.GONE);
        alignManualControlView(false);
    }

    public void toggleOIS() {
        setOIS(!mSettings.cameraStartupSettings.ois, false);
    }

    public void setOIS(boolean ois, boolean uiOnly) {
        if(mSettings.cameraStartupSettings.ois == ois)
            return;

        if(mActiveCamera != null && !uiOnly) {
            mActiveCamera.setOIS(ois);
            mActiveCamera.activateCameraSettings();
        }

        mSettings.cameraStartupSettings.ois = ois;
    }

    public void toggleAWB() {
        if(mActiveCamera == null) {
            return;
        }

        mAWBLock = !mAWBLock;
        mActiveCamera.setAWBLock(mAWBLock);

        mActiveCamera.activateCameraSettings();

        int color = mAWBLock ? R.color.colorAccent : R.color.white;
        int drawable = mAWBLock ? R.drawable.lock : R.drawable.lock_open;

        ((ImageView) mActivity.findViewById(R.id.awbLockBtnIcon)).setImageDrawable(AppCompatResources.getDrawable(mActivity, drawable));
        ((TextView) mActivity.findViewById(R.id.awbLockBtnText)).setTextColor(mActivity.getColor(color));
    }

    public void toggleAE() {
        if(mActiveCamera == null) {
            return;
        }

        boolean lock = !(mExposureState == NativeCamera.CameraExposureState.LOCKED);

        mActiveCamera.setAELock(lock);
        mActiveCamera.activateCameraSettings();
    }

    private void setFocusMode(FocusMode mode, PointF focusPt) {
        if(mActiveCamera == null)
            return;

        if(mFocusMode == FocusMode.CONTINUOUS && mode == FocusMode.CONTINUOUS)
            return;

        // Don't update if the focus points are very close to each other
        if(focusPt != null && mUserFocusPt != null) {
            double d = Math.hypot(focusPt.x - mUserFocusPt.x, focusPt.y - mUserFocusPt.y);
            if(d < 0.05) {
                return;
            }
        }

        if(mode == FocusMode.USER_SELECTED && focusPt != null) {
            mUserFocusRequestedTimestampMs = System.currentTimeMillis();
            mSettings.cameraStartupSettings.useUserExposureSettings = false;

            mActiveCamera.setFocusPoint(focusPt, focusPt);
        }
        else if(mode == FocusMode.CONTINUOUS) {
            mUserFocusRequestedTimestampMs = -1;
            mSettings.cameraStartupSettings.useUserExposureSettings = false;

            mActiveCamera.setAutoFocus();
        }
        else if(mode == FocusMode.MANUAL) {
            mActiveCamera.setManualFocus(mFocusDistance);
            updateFocusDistance();
        }

        mUserFocusPt = focusPt;
        mUserRequestedAeAfLock = false;
        mFocusMode = mode;
        mActiveCamera.setAELock(false);

        mActiveCamera.activateCameraSettings();
    }

    private void onUserTouchFocus(float touchX, float touchY) {
        if(mActiveCamera == null || mActiveCameraMetadata == null)
            return;

        // If settings AF regions is not supported, do nothing
        if(mActiveCameraMetadata.maxAfRegions <= 0)
            return;

        float x = touchX / mBinding.cameraFrame.getWidth();
        float y = touchY / mBinding.cameraFrame.getHeight();

        // Ignore edges
        if (x < 0.05 || x > 0.95 || y < 0.05 || y > 0.95)
        {
            return;
        }

        Matrix m = new Matrix();
        float[] pts = new float[] { x, y };

        m.setRotate(-mActiveCameraMetadata.sensorOrientation, 0.5f, 0.5f);
        m.mapPoints(pts);

        // Update UI
        FrameLayout.LayoutParams layoutParams =
                (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

        layoutParams.setMargins(
                Math.round(touchX) - mBinding.focusLockPointFrame.getWidth() / 2,
                Math.round(touchY) - mBinding.focusLockPointFrame.getHeight() / 2,
                0,
                0);

        mBinding.focusLockPointFrame.setScaleX(1.25f);
        mBinding.focusLockPointFrame.setScaleY(1.25f);
        mBinding.focusLockPointFrame.setAlpha(1.0f);
        mBinding.focusLockPointFrame.setLayoutParams(layoutParams);
        mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
        mBinding.focusLockPointFrame.getDrawable().setTint(mActivity.getColor(R.color.white));

        mBinding.focusLockPointFrame.animate().cancel();
        mBinding.focusLockPointFrame
                .animate()
                .alpha(1.0f)
                .scaleX(1.0f)
                .scaleY(1.0f)
                .setDuration(250)
                .start();

        setFocusMode(FocusMode.USER_SELECTED, new PointF(pts[0], pts[1]));
    }

    @Override
    public void onCameraStarted() {
        mBinding.manualControlsFrame.setVisibility(View.VISIBLE);

        setOIS(mSettings.cameraStartupSettings.ois, true);
        alignManualControlView(false);
    }

    @Override
    public void onCameraDisconnected() {
    }

    @Override
    public void onCameraError(int error) {
    }

    @Override
    public void onCameraSessionStateChanged(NativeCamera.CameraState state) {
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
        final int cameraIso = CameraManualControl.GetClosestIso(mIsoValues, iso);
        final CameraManualControl.SHUTTER_SPEED cameraShutterSpeed = CameraManualControl.GetClosestShutterSpeed(exposureTime);

        int color = mSettings.cameraStartupSettings.useUserExposureSettings ?
                mActivity.getColor(R.color.colorAccent) : mActivity.getColor(R.color.white);

        TextView isoBtn = mActivity.findViewById(R.id.isoBtn);
        TextView shutterSpeedBtn = mActivity.findViewById(R.id.shutterSpeedBtn);

        isoBtn.setText(String.valueOf(cameraIso));
        isoBtn.setTextColor(color);

        shutterSpeedBtn.setText(cameraShutterSpeed.toString());
        shutterSpeedBtn.setTextColor(color);

        mSettings.cameraStartupSettings.iso = iso;
        mSettings.cameraStartupSettings.exposureTime = exposureTime;
    }

    @Override
    public void onCameraAutoFocusStateChanged(NativeCamera.CameraFocusState state, float focusDistance) {
        Log.i(TAG, "Focus state: " + state.name());

        // Focus circle
        if( state == NativeCamera.CameraFocusState.PASSIVE_SCAN ||
            state == NativeCamera.CameraFocusState.ACTIVE_SCAN)
        {
            mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
            mBinding.focusLockPointFrame.setAlpha(1.0f);

            // Center focus circle
            if(mFocusMode == FocusMode.CONTINUOUS) {
                FrameLayout.LayoutParams layoutParams =
                        (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

                layoutParams.setMargins(
                        (mBinding.cameraFrame.getWidth() - mBinding.focusLockPointFrame.getWidth()) / 2,
                        (mBinding.cameraFrame.getHeight() - mBinding.focusLockPointFrame.getHeight()) / 2,
                        0,
                        0);

                mBinding.focusLockPointFrame.setLayoutParams(layoutParams);
            }
        }
        else if(state == NativeCamera.CameraFocusState.PASSIVE_FOCUSED ||
                state == NativeCamera.CameraFocusState.FOCUS_LOCKED)
        {
            float alpha = mFocusMode == FocusMode.CONTINUOUS ? 0.0f : 0.5f;

            mBinding.focusLockPointFrame.animate().cancel();
            mBinding.focusLockPointFrame.getDrawable().setTint(mActivity.getColor(R.color.white));
            mBinding.focusLockPointFrame
                    .animate()
                    .alpha(alpha)
                    .scaleX(1.0f)
                    .scaleY(1.0f)
                    .setDuration(250)
                    .start();
        }

        // Keep state
        mFocusDistance = focusDistance;
        mCameraFocusState = state;

        // Update focus lock indicator
        if(mUserRequestedAeAfLock)
            attemptAeAfLock();

        updateFocusDistance();
    }

    private void updateFocusDistance() {
        String focusDistanceMetersText;
        if(mFocusDistance > 0) {
            float focusDistanceMeters = 1.0f / mFocusDistance;
            focusDistanceMetersText = String.format(Locale.US, "%.2f M", focusDistanceMeters);
        }
        else {
            focusDistanceMetersText = "-";
        }

        TextView focusBtn = mActivity.findViewById(R.id.focusBtn);
        int color = mFocusMode == FocusMode.MANUAL ?
                mActivity.getColor(R.color.colorAccent) : mActivity.getColor(R.color.white);

        focusBtn.setTextColor(color);
        focusBtn.setText(focusDistanceMetersText);
    }

    private void attemptAeAfLock() {
        if(mActiveCamera == null)
            return;

        boolean aeLocked = (
                mExposureState == NativeCamera.CameraExposureState.CONVERGED
            ||  mExposureState == NativeCamera.CameraExposureState.LOCKED
            ||  (mExposureState == NativeCamera.CameraExposureState.INACTIVE && mExposureMode == ExposureMode.MANUAL) );

        boolean focusLocked = (
                mCameraFocusState == NativeCamera.CameraFocusState.FOCUS_LOCKED
            ||  mCameraFocusState == NativeCamera.CameraFocusState.PASSIVE_FOCUSED );

        if(mUserRequestedAeAfLock && focusLocked && aeLocked) {
            mFocusMode = FocusMode.USER_LOCKED;
            mUserRequestedAeAfLock = false;

            mActiveCamera.setAELock(true);
            mActiveCamera.setManualFocus(mFocusDistance);

            mActiveCamera.activateCameraSettings();

            mBinding.focusLockPointFrame.getDrawable()
                    .setTint(mActivity.getColor(R.color.focusUserLocked));

            // Don't want this crashing the app
            try {
                Vibrator v = (Vibrator) mActivity.getSystemService(Context.VIBRATOR_SERVICE);
                v.vibrate(10);
            }
            catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public void updateOrientation(NativeCameraBuffer.ScreenOrientation orientation) {
        mOrientation = orientation;
    }

    public void alignManualControlView(boolean animate) {
        mActivity.findViewById(R.id.manualControlFocus).post(() -> doAlignManualControlView(mOrientation, animate));
        doAlignManualControlView(mOrientation, animate);
    }

    private void doAlignManualControlView(NativeCameraBuffer.ScreenOrientation orientation, boolean animate) {
        if(mBinding.manualControlsFrame.getVisibility() != View.VISIBLE)
            return;

        ViewGroup.LayoutParams params = mBinding.manualControlsFrame.getLayoutParams();

        if(     orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE
                ||  orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE)
        {
            params.width = mBinding.cameraFrame.getHeight();
        }
        else {
            params.width = mBinding.cameraFrame.getWidth();
        }

        mBinding.manualControlsFrame.setLayoutParams(params);

        final int rotation;
        final int translationX;
        final int translationY;

        // Update position of manual controls
        if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {
            rotation = 180;
            translationX = 0;
            translationY = -(mBinding.cameraFrame.getHeight() - mBinding.manualControlsFrame.getHeight()) / 2;
        }
        else if(orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE) {
            rotation = 90;
            translationX = -mBinding.cameraFrame.getWidth()/2 + mBinding.manualControlsFrame.getHeight()/2;
            translationY = 0;
        }
        else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE) {
            rotation = -90;
            translationX = mBinding.cameraFrame.getWidth()/2 - mBinding.manualControlsFrame.getHeight()/2;
            translationY = 0;
        }
        else {
            // Portrait
            rotation = 0;
            translationX = 0;
            translationY = (mBinding.cameraFrame.getHeight() - mBinding.manualControlsFrame.getHeight()) / 2;
        }

        if(animate) {
            mBinding.manualControlsFrame.animate()
                    .rotation(rotation)
                    .translationX(translationX)
                    .translationY(translationY)
                    .setDuration(250)
                    .start();
        }
        else {
            mBinding.manualControlsFrame.setRotation(rotation);
            mBinding.manualControlsFrame.setTranslationX(translationX);
            mBinding.manualControlsFrame.setTranslationY(translationY);
        }
    }

    @Override
    public void onCameraAutoExposureStateChanged(NativeCamera.CameraExposureState state) {
        Log.i(TAG, "Exposure state: " + state.name());

        boolean timePassed = (System.currentTimeMillis() - mUserFocusRequestedTimestampMs) > USER_SELECTED_FOCUS_CANCEL_TIME_MS;

        // Reset back to continuous focus mode if the the exposure has changed after a while
        if(state == NativeCamera.CameraExposureState.SEARCHING
                && timePassed
                && mFocusMode == FocusMode.USER_SELECTED)
        {
            setFocusMode(FocusMode.CONTINUOUS, null);
        }

        // AE lock indicator
        boolean aeLocked = state == NativeCamera.CameraExposureState.LOCKED;

        int color = aeLocked ? R.color.colorAccent : R.color.white;
        int drawable = aeLocked ? R.drawable.lock : R.drawable.lock_open;

        ((ImageView) mActivity.findViewById(R.id.aeLockBtnIcon)).setImageDrawable(AppCompatResources.getDrawable(mActivity, drawable));
        ((TextView) mActivity.findViewById(R.id.aeLockBtnText)).setTextColor(mActivity.getColor(color));

        mExposureState = state;

        if(mUserRequestedAeAfLock)
            attemptAeAfLock();
    }

    @Override
    public void onCameraHdrImageCaptureProgress(int progress) {
    }

    @Override
    public void onCameraHdrImageCaptureFailed() {
    }

    @Override
    public void onCameraHdrImageCaptureCompleted() {
    }

    @Override
    public void onMemoryAdjusting() {
    }

    @Override
    public void onMemoryStable() {
    }

    @Override
    public boolean onDown(MotionEvent event) {
        if(event.getAction() == MotionEvent.ACTION_DOWN) {
            onUserTouchFocus(event.getX(), event.getY());
            return true;
        }

        return false;
    }

    @Override
    public void onShowPress(MotionEvent motionEvent) {
    }

    @Override
    public boolean onSingleTapUp(MotionEvent motionEvent) {
        return false;
    }

    @Override
    public boolean onScroll(MotionEvent motionEvent, MotionEvent motionEvent1, float v, float v1) {
        return false;
    }

    @Override
    public void onLongPress(MotionEvent event) {
        if(event.getAction() == MotionEvent.ACTION_DOWN) {
            mUserRequestedAeAfLock = true;
            attemptAeAfLock();
        }
    }

    @Override
    public boolean onFling(MotionEvent motionEvent, MotionEvent motionEvent1, float v, float v1) {
        return false;
    }

    public void onTouch(MotionEvent motionEvent) {
        if(motionEvent.getAction() == MotionEvent.ACTION_UP)
            mUserRequestedAeAfLock = false;
    }
}
