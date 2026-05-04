package com.syncflow

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.syncflow.databinding.ActivityDeviceInfoBinding

class DeviceInfoActivity : AppCompatActivity() {
    private lateinit var binding: ActivityDeviceInfoBinding

    external fun nativeGetStatus(): String

    companion object {
        init {
            System.loadLibrary("syncflowjni")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityDeviceInfoBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnRefresh.setOnClickListener {
            refreshStatus()
        }

        refreshStatus()
    }

    private fun refreshStatus() {
        val s = try { nativeGetStatus() } catch (e: Throwable) { "native not available" }
        binding.tvStatus.text = s
    }
}
